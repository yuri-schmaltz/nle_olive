#include "testsignal.h"
#include <cmath>
#include <algorithm>
namespace olive {
const QString ToneGenerator::kFrequency = QStringLiteral("frequency_in");
const QString ToneGenerator::kAmplitude = QStringLiteral("amplitude_in");
ToneGenerator::ToneGenerator() {
  AddInput(kFrequency, NodeValue::kFloat, 1000.0, InputFlags(kInputFlagNotKeyframable));
  SetInputProperty(kFrequency, QStringLiteral("min"), 0.0);
  AddInput(kAmplitude, NodeValue::kFloat, 0.1);
  SetInputProperty(kAmplitude, QStringLiteral("min"), 0.0);
  SetInputProperty(kAmplitude, QStringLiteral("max"), 1.0);
}
void ToneGenerator::Retranslate() {
  Node::Retranslate();
  SetInputName(kFrequency, tr("Frequency (Hz)"));
  SetInputName(kAmplitude, tr("Amplitude"));
}
void ToneGenerator::Value(const NodeValueRow &row, const NodeGlobals &globals, NodeValueTable *table) const {
  const AudioParams &params = globals.aparams();
  if (!params.is_valid() || globals.time().length() <= rational(0)) return;
  SampleBuffer samples(params, globals.time().length());
  samples.silence();
  const double frequency = row[kFrequency].toDouble();
  const double level = row[kAmplitude].toDouble();
  if (std::isfinite(frequency) && std::isfinite(level) && frequency >= 0
      && frequency < params.sample_rate() / 2.0) {
    const qint64 first = params.time_to_samples(globals.time().in());
    const double amplitude = std::clamp(level, 0.0, 1.0);
    const double period = 2.0 * std::acos(-1.0) * frequency / params.sample_rate();
    for (size_t i = 0; i < samples.sample_count(); ++i) {
      const float value = float(amplitude * std::sin(period * (first + qint64(i))));
      for (int c = 0; c < params.channel_count(); ++c) samples.data(c)[i] = value;
    }
  }
  table->Push(NodeValue::kSamples, samples, this);
}
void BarsGenerator::Value(const NodeValueRow &row, const NodeGlobals &globals, NodeValueTable *table) const {
  table->Push(NodeValue::kTexture, Texture::Job(globals.vparams(), ShaderJob(row)), this);
}
ShaderCode BarsGenerator::GetShaderCode(const ShaderRequest&) const {
  return ShaderCode(QStringLiteral(R"(
in vec2 ove_texcoord;
out vec4 frag_color;
void main() {
  int bar = clamp(int(ove_texcoord.x * 8.0), 0, 7);
  vec3 colors[8] = vec3[8](vec3(1,1,1), vec3(1,1,0), vec3(0,1,1), vec3(0,1,0),
                           vec3(1,0,1), vec3(1,0,0), vec3(0,0,1), vec3(0,0,0));
  frag_color = vec4(colors[bar], 1.0);
}
)"));
}
}
