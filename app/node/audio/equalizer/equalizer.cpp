#include "equalizer.h"

#include "render/job/samplejob.h"

namespace olive {

const QString EqualizerNode::kSamplesInput = QStringLiteral("samples_in");

const BiquadFilterType EqualizerNode::kDefaultTypes[kBandCount] = {
  kFilterLowShelf,
  kFilterPeaking,
  kFilterPeaking,
  kFilterPeaking,
  kFilterPeaking,
  kFilterHighShelf
};

const double EqualizerNode::kDefaultFrequencies[kBandCount] = {
  80.0,
  250.0,
  1000.0,
  4000.0,
  8000.0,
  12000.0
};

const double EqualizerNode::kDefaultQs[kBandCount] = {
  0.7071,
  1.0,
  1.0,
  1.0,
  1.0,
  0.7071
};

#define super Node

EqualizerNode::EqualizerNode()
{
  AddInput(kSamplesInput, NodeValue::kSamples, InputFlags(kInputFlagNotKeyframable));

  for (int i = 0; i < kBandCount; ++i) {
    const QString enabled_id = BandEnabledInput(i);
    const QString type_id = BandTypeInput(i);
    const QString freq_id = BandFreqInput(i);
    const QString gain_id = BandGainInput(i);
    const QString q_id = BandQInput(i);

    // Enabled input: bool
    AddInput(enabled_id, NodeValue::kBoolean, true);

    // Type input: combo
    AddInput(type_id, NodeValue::kCombo, static_cast<int>(kDefaultTypes[i]),
             InputFlags(kInputFlagNotConnectable | kInputFlagNotKeyframable));

    // Frequency input: float
    AddInput(freq_id, NodeValue::kFloat, kDefaultFrequencies[i]);
    SetInputProperty(freq_id, QStringLiteral("min"), 10.0);
    SetInputProperty(freq_id, QStringLiteral("max"), 20000.0);

    // Gain input: float (dB)
    AddInput(gain_id, NodeValue::kFloat, 0.0);
    SetInputProperty(gain_id, QStringLiteral("min"), -24.0);
    SetInputProperty(gain_id, QStringLiteral("max"), 24.0);

    // Q factor input: float
    AddInput(q_id, NodeValue::kFloat, kDefaultQs[i]);
    SetInputProperty(q_id, QStringLiteral("min"), 0.1);
    SetInputProperty(q_id, QStringLiteral("max"), 10.0);
  }

  SetFlag(kAudioEffect);
  SetEffectInput(kSamplesInput);
}

QString EqualizerNode::Name() const
{
  return tr("Parametric Equalizer");
}

QString EqualizerNode::id() const
{
  return QStringLiteral("org.olivevideoeditor.Olive.equalizer");
}

QVector<Node::CategoryID> EqualizerNode::Category() const
{
  return {kCategoryFilter};
}

QString EqualizerNode::Description() const
{
  return tr("Adjusts the frequency balance of an audio source using 6-band parametric biquad filters.");
}

QString EqualizerNode::BandEnabledInput(int band)
{
  return QStringLiteral("band%1_enabled_in").arg(band + 1);
}

QString EqualizerNode::BandTypeInput(int band)
{
  return QStringLiteral("band%1_type_in").arg(band + 1);
}

QString EqualizerNode::BandFreqInput(int band)
{
  return QStringLiteral("band%1_freq_in").arg(band + 1);
}

QString EqualizerNode::BandGainInput(int band)
{
  return QStringLiteral("band%1_gain_in").arg(band + 1);
}

QString EqualizerNode::BandQInput(int band)
{
  return QStringLiteral("band%1_q_in").arg(band + 1);
}

void EqualizerNode::SetBandEnabled(int band, bool enabled)
{
  SetStandardValue(BandEnabledInput(band), enabled);
}

bool EqualizerNode::GetBandEnabled(int band) const
{
  return GetStandardValue(BandEnabledInput(band)).toBool();
}

void EqualizerNode::SetBandType(int band, BiquadFilterType type)
{
  SetStandardValue(BandTypeInput(band), static_cast<int>(type));
}

BiquadFilterType EqualizerNode::GetBandType(int band) const
{
  return static_cast<BiquadFilterType>(GetStandardValue(BandTypeInput(band)).toInt());
}

void EqualizerNode::SetBandFrequency(int band, double freq)
{
  SetStandardValue(BandFreqInput(band), freq);
}

double EqualizerNode::GetBandFrequency(int band) const
{
  return GetStandardValue(BandFreqInput(band)).toDouble();
}

void EqualizerNode::SetBandGain(int band, double gain)
{
  SetStandardValue(BandGainInput(band), gain);
}

double EqualizerNode::GetBandGain(int band) const
{
  return GetStandardValue(BandGainInput(band)).toDouble();
}

void EqualizerNode::SetBandQ(int band, double q)
{
  SetStandardValue(BandQInput(band), q);
}

double EqualizerNode::GetBandQ(int band) const
{
  return GetStandardValue(BandQInput(band)).toDouble();
}

void EqualizerNode::Value(const NodeValueRow &value, const NodeGlobals &globals, NodeValueTable *table) const
{
  SampleBuffer buffer = value[kSamplesInput].toSamples();

  if (!buffer.is_allocated() || buffer.sample_count() == 0 || buffer.channel_count() == 0) {
    table->Push(NodeValue::kSamples, buffer, this);
    return;
  }

  bool enabled = true;
  if (value.contains(kEnabledInput) && value[kEnabledInput].type() != NodeValue::kNone) {
    enabled = value[kEnabledInput].toBool();
  } else {
    enabled = GetStandardValue(kEnabledInput).toBool();
  }

  if (!enabled) {
    table->Push(NodeValue::kSamples, buffer, this);
    return;
  }

  // Fast check: are all active band parameters static?
  bool is_static = true;
  for (int i = 0; i < kBandCount; ++i) {
    if (!IsInputStatic(BandEnabledInput(i)) ||
        !IsInputStatic(BandFreqInput(i)) ||
        !IsInputStatic(BandGainInput(i)) ||
        !IsInputStatic(BandQInput(i))) {
      is_static = false;
      break;
    }
  }

  if (is_static) {
    ProcessBuffer(buffer, value);
    table->Push(NodeValue::kSamples, buffer, this);
  } else {
    SampleJob job(globals.time(), kSamplesInput, value);
    for (int i = 0; i < kBandCount; ++i) {
      job.Insert(BandEnabledInput(i), value);
      job.Insert(BandTypeInput(i), value);
      job.Insert(BandFreqInput(i), value);
      job.Insert(BandGainInput(i), value);
      job.Insert(BandQInput(i), value);
    }
    table->Push(NodeValue::kSamples, job, this);
  }
}

void EqualizerNode::ProcessBuffer(SampleBuffer &buffer, const NodeValueRow &values) const
{
  const double sample_rate = buffer.audio_params().sample_rate();
  const size_t sample_count = buffer.sample_count();
  const int channels = buffer.channel_count();

  BiquadCoeffs active_coeffs[kBandCount];
  int active_count = 0;

  for (int i = 0; i < kBandCount; ++i) {
    const QString enabled_id = BandEnabledInput(i);
    bool enabled = values.contains(enabled_id) && values[enabled_id].type() != NodeValue::kNone
      ? values[enabled_id].toBool()
      : GetStandardValue(enabled_id).toBool();
    if (!enabled) {
      continue;
    }

    const QString type_id = BandTypeInput(i);
    const QString freq_id = BandFreqInput(i);
    const QString gain_id = BandGainInput(i);
    const QString q_id = BandQInput(i);

    auto type = static_cast<BiquadFilterType>(
      values.contains(type_id) && values[type_id].type() != NodeValue::kNone
        ? values[type_id].toInt()
        : GetStandardValue(type_id).toInt());

    double freq = values.contains(freq_id) && values[freq_id].type() != NodeValue::kNone
      ? values[freq_id].toDouble()
      : GetStandardValue(freq_id).toDouble();

    double gain = values.contains(gain_id) && values[gain_id].type() != NodeValue::kNone
      ? values[gain_id].toDouble()
      : GetStandardValue(gain_id).toDouble();

    double q = values.contains(q_id) && values[q_id].type() != NodeValue::kNone
      ? values[q_id].toDouble()
      : GetStandardValue(q_id).toDouble();

    // Peaking / Shelving filters with 0 dB gain are transparent wires
    if ((type == kFilterPeaking || type == kFilterLowShelf || type == kFilterHighShelf) &&
        std::abs(gain) < 1e-4) {
      continue;
    }

    active_coeffs[active_count++] = BiquadCoeffs::Calculate(type, sample_rate, freq, gain, q);
  }

  if (active_count == 0) {
    return;
  }

  for (int c = 0; c < channels; ++c) {
    float *channel_data = buffer.data(c);
    for (int b = 0; b < active_count; ++b) {
      Biquad::ProcessChannel(channel_data, sample_count, active_coeffs[b]);
    }
  }
}

void EqualizerNode::ProcessSamples(const NodeValueRow &values, const SampleBuffer &input, SampleBuffer &output, int index) const
{
  thread_local BiquadState tl_states[kBandCount][8];

  const int channels = std::min(input.audio_params().channel_count(), 8);
  const double sample_rate = input.audio_params().sample_rate();

  // Reset states on the first sample of a block
  if (index == 0) {
    for (int b = 0; b < kBandCount; ++b) {
      for (int c = 0; c < 8; ++c) {
        tl_states[b][c].Reset();
      }
    }
  }

  for (int c = 0; c < channels; ++c) {
    float s = input.data(c)[index];

    for (int b = 0; b < kBandCount; ++b) {
      const QString enabled_id = BandEnabledInput(b);
      bool enabled = values.contains(enabled_id) && values[enabled_id].type() != NodeValue::kNone
        ? values[enabled_id].toBool()
        : GetStandardValue(enabled_id).toBool();
      if (!enabled) {
        continue;
      }

      const QString type_id = BandTypeInput(b);
      const QString freq_id = BandFreqInput(b);
      const QString gain_id = BandGainInput(b);
      const QString q_id = BandQInput(b);

      auto type = static_cast<BiquadFilterType>(
        values.contains(type_id) && values[type_id].type() != NodeValue::kNone
          ? values[type_id].toInt()
          : GetStandardValue(type_id).toInt());

      double freq = values.contains(freq_id) && values[freq_id].type() != NodeValue::kNone
        ? values[freq_id].toDouble()
        : GetStandardValue(freq_id).toDouble();

      double gain = values.contains(gain_id) && values[gain_id].type() != NodeValue::kNone
        ? values[gain_id].toDouble()
        : GetStandardValue(gain_id).toDouble();

      double q = values.contains(q_id) && values[q_id].type() != NodeValue::kNone
        ? values[q_id].toDouble()
        : GetStandardValue(q_id).toDouble();

      BiquadCoeffs coeff = BiquadCoeffs::Calculate(type, sample_rate, freq, gain, q);
      s = Biquad::ProcessSample(s, coeff, tl_states[b][c]);
    }

    output.data(c)[index] = s;
  }

  for (int c = channels; c < input.audio_params().channel_count(); ++c) {
    output.data(c)[index] = input.data(c)[index];
  }
}

void EqualizerNode::Retranslate()
{
  super::Retranslate();

  SetInputName(kSamplesInput, tr("Samples"));

  const QStringList type_strings = {
    tr("Low Shelf"),
    tr("High Shelf"),
    tr("Peaking (Bell)"),
    tr("Low Pass"),
    tr("High Pass"),
    tr("Notch")
  };

  for (int i = 0; i < kBandCount; ++i) {
    const int num = i + 1;
    SetInputName(BandEnabledInput(i), tr("Band %1 Enabled").arg(num));
    SetInputName(BandTypeInput(i), tr("Band %1 Type").arg(num));
    SetComboBoxStrings(BandTypeInput(i), type_strings);
    SetInputName(BandFreqInput(i), tr("Band %1 Frequency").arg(num));
    SetInputName(BandGainInput(i), tr("Band %1 Gain").arg(num));
    SetInputName(BandQInput(i), tr("Band %1 Q").arg(num));
  }
}

} // namespace olive
