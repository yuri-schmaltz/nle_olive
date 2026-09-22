/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "testutil.h"
#include <cmath>
#include "render/colorprocessor.h"

#include "node/color/colorwheels/colorwheels.h"
#include "node/distort/crop/cropdistortnode.h"
#include "node/distort/transform/transformdistortnode.h"
#include "node/factory.h"
#include "node/generator/solid/solid.h"
#include "node/generator/testsignal/testsignal.h"
#include "node/math/merge/merge.h"
#include "node/project.h"
#include "render/rendermanager.h"

namespace olive {
OLIVE_ADD_TEST(ColorDisplayRoundTrip)
{
  ColorManager::SetUpDefaultConfig();
  Project project;
  auto *manager = project.color_manager();
  const ColorTransform transform(manager->GetDefaultDisplay(),
                                 manager->GetDefaultView(manager->GetDefaultDisplay()), QString());
  auto forward = ColorProcessor::Create(manager, manager->GetReferenceColorSpace(), transform);
  auto inverse = ColorProcessor::Create(manager, manager->GetReferenceColorSpace(), transform, ColorProcessor::kInverse);
  for (float value : {0.01f, 0.18f, 0.5f, 0.9f}) {
    const Color source(value, value * 0.5f, value * 0.25f, 0.5f);
    const Color result = inverse->ConvertColor(forward->ConvertColor(source));
    OLIVE_ASSERT(std::fabs(result.red() - source.red()) < 1e-4);
    OLIVE_ASSERT(std::fabs(result.green() - source.green()) < 1e-4);
    OLIVE_ASSERT(std::fabs(result.blue() - source.blue()) < 1e-4);
    OLIVE_ASSERT(std::fabs(result.alpha() - source.alpha()) < 1e-6);
  }
  OLIVE_TEST_END;
}
OLIVE_ADD_TEST(InvalidColorSpaceReportsError)
{
  ColorManager::SetUpDefaultConfig();
  Project project;
  bool caught = false;
  try {
    ColorProcessor::Create(project.color_manager(), QStringLiteral("missing-colorspace"),
                           project.color_manager()->GetReferenceColorSpace());
  } catch (const OCIO::Exception&) { caught = true; }
  OLIVE_ASSERT(caught);
  OLIVE_TEST_END;
}
OLIVE_ADD_TEST(MergeEmptyAndSingleLayer)
{
  MergeNode merge;
  const VideoParams params(16, 16, PixelFormat::F32, 4);
  auto texture = std::make_shared<Texture>(params);
  NodeGlobals globals(params, AudioParams(), TimeRange(0, 1), LoopMode::kLoopModeOff);
  NodeValueTable table;
  merge.Value({}, globals, &table);
  OLIVE_ASSERT(!table.Get(NodeValue::kTexture).toTexture());
  NodeValueRow row;
  row.insert(MergeNode::kBaseIn, NodeValue(NodeValue::kTexture, texture));
  merge.Value(row, globals, &table);
  OLIVE_ASSERT(table.Get(NodeValue::kTexture).toTexture() == texture);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(ToneGeneratorContinuousPhase)
{
  ToneGenerator tone;
  const AudioParams aparams(48000, AV_CH_LAYOUT_STEREO, SampleFormat::F32);
  const VideoParams vparams(16, 16, PixelFormat::F32, 4);

  // Generate buffer across [0, 1/10s]
  NodeGlobals globals1(vparams, aparams, TimeRange(rational(0), rational(1, 10)), LoopMode::kLoopModeOff);
  NodeValueTable table1;
  NodeValueRow row;
  row.insert(ToneGenerator::kFrequency, NodeValue(NodeValue::kFloat, 440.0));
  row.insert(ToneGenerator::kAmplitude, NodeValue(NodeValue::kFloat, 0.5));
  tone.Value(row, globals1, &table1);

  SampleBuffer s1 = table1.Get(NodeValue::kSamples).toSamples();
  OLIVE_ASSERT(s1.sample_count() == 4800);
  OLIVE_ASSERT_EQUAL(s1.channel_count(), 2);

  // Generate contiguous buffer across [1/10s, 2/10s]
  NodeGlobals globals2(vparams, aparams, TimeRange(rational(1, 10), rational(2, 10)), LoopMode::kLoopModeOff);
  NodeValueTable table2;
  tone.Value(row, globals2, &table2);

  SampleBuffer s2 = table2.Get(NodeValue::kSamples).toSamples();
  OLIVE_ASSERT(s2.sample_count() == 4800);

  // Check phase continuity at boundary: s1 last sample and s2 first sample
  const double period = 2.0 * std::acos(-1.0) * 440.0 / 48000.0;
  const float expected_boundary = float(0.5 * std::sin(period * 4800));
  OLIVE_ASSERT(std::fabs(s2.data(0)[0] - expected_boundary) < 1e-4);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(BarsGeneratorProvidesShader)
{
  BarsGenerator bars;
  const VideoParams vparams(64, 64, PixelFormat::F32, 4);
  NodeGlobals globals(vparams, AudioParams(), TimeRange(0, 1), LoopMode::kLoopModeOff);
  NodeValueTable table;
  bars.Value({}, globals, &table);

  TexturePtr tex = table.Get(NodeValue::kTexture).toTexture();
  OLIVE_ASSERT(tex != nullptr);

  Node::ShaderRequest request(QStringLiteral("bars"));
  ShaderCode code = bars.GetShaderCode(request);
  OLIVE_ASSERT(!code.frag_code().isEmpty());
  OLIVE_ASSERT(code.frag_code().contains("frag_color"));

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(ColorWheelsNodeShaderAndProperties)
{
  NodeFactory::Initialize();
  Node *created = NodeFactory::CreateFromID(QStringLiteral("org.olivevideoeditor.Olive.colorwheels"));
  OLIVE_ASSERT(created != nullptr);
  OLIVE_ASSERT_EQUAL(created->Name(), QStringLiteral("Color Wheels (Lift/Gamma/Gain)"));
  OLIVE_ASSERT(created->GetFlags() & Node::kVideoEffect);
  delete created;

  ColorWheelsNode wheels;
  OLIVE_ASSERT(wheels.HasInputWithID(ColorWheelsNode::kTextureInput));
  OLIVE_ASSERT(wheels.HasInputWithID(ColorWheelsNode::kLiftInput));
  OLIVE_ASSERT(wheels.HasInputWithID(ColorWheelsNode::kGammaInput));
  OLIVE_ASSERT(wheels.HasInputWithID(ColorWheelsNode::kGainInput));
  OLIVE_ASSERT(wheels.HasInputWithID(ColorWheelsNode::kOffsetInput));

  Node::ShaderRequest request(QStringLiteral("colorwheels"));
  ShaderCode code = wheels.GetShaderCode(request);
  OLIVE_ASSERT(!code.frag_code().isEmpty());
  OLIVE_ASSERT(code.frag_code().contains("lift_in"));
  OLIVE_ASSERT(code.frag_code().contains("gain_in"));
  OLIVE_ASSERT(code.frag_code().contains("gamma_in"));
  OLIVE_ASSERT(code.frag_code().contains("offset_in"));

  // Check processing pass-through when texture is provided
  const VideoParams vparams(16, 16, PixelFormat::F32, 4);
  auto texture = std::make_shared<Texture>(vparams);
  NodeGlobals globals(vparams, AudioParams(), TimeRange(0, 1), LoopMode::kLoopModeOff);
  NodeValueTable table;
  NodeValueRow row;
  row.insert(ColorWheelsNode::kTextureInput, NodeValue(NodeValue::kTexture, texture));
  wheels.Value(row, globals, &table);

  TexturePtr out_tex = table.Get(NodeValue::kTexture).toTexture();
  OLIVE_ASSERT(out_tex != nullptr);

  NodeFactory::Destroy();
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(OCIONonInvertibleTransformFallback)
{
  ColorManager::SetUpDefaultConfig();
  Project project;
  auto *manager = project.color_manager();

  // Try to create an inverse processor from the reference colorspace to itself
  // with a bad display transform - should throw OCIO::Exception, not crash
  bool caught = false;
  try {
    // Trying to invert a display transform from linear->linear (non-invertible path)
    ColorTransform bad_transform(
        QStringLiteral("__nonexistent_display__"),
        QStringLiteral("__nonexistent_view__"),
        QString());
    ColorProcessor::Create(manager, manager->GetReferenceColorSpace(),
                           bad_transform, ColorProcessor::kInverse);
  } catch (const OCIO::Exception&) {
    caught = true;
  } catch (...) {
    caught = true;  // Any exception is acceptable — we must NOT crash
  }
  OLIVE_ASSERT(caught);

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(ToneGeneratorSampleRateBoundaries)
{
  ToneGenerator tone;
  const AudioParams aparams(44100, AV_CH_LAYOUT_MONO, SampleFormat::F32);
  const VideoParams vparams(16, 16, PixelFormat::F32, 4);

  NodeValueRow row;
  // Nyquist frequency = half sample rate = 22050 Hz
  row.insert(ToneGenerator::kFrequency, NodeValue(NodeValue::kFloat, 22050.0));
  row.insert(ToneGenerator::kAmplitude, NodeValue(NodeValue::kFloat, 1.0));

  NodeGlobals globals(vparams, aparams, TimeRange(rational(0), rational(1, 100)), LoopMode::kLoopModeOff);
  NodeValueTable table;
  tone.Value(row, globals, &table);

  SampleBuffer buf = table.Get(NodeValue::kSamples).toSamples();
  // 1/100s at 44100 Hz = 441 samples
  OLIVE_ASSERT_EQUAL(buf.sample_count(), 441);
  OLIVE_ASSERT_EQUAL(buf.channel_count(), 1);

  // All samples must be finite (no NaN/Inf at Nyquist)
  for (int i = 0, n = static_cast<int>(buf.sample_count()); i < n; ++i) {
    OLIVE_ASSERT(std::isfinite(buf.data(0)[i]));
  }

  // Test amplitude = 0: all samples must be exactly 0
  NodeValueRow silent_row;
  silent_row.insert(ToneGenerator::kFrequency, NodeValue(NodeValue::kFloat, 440.0));
  silent_row.insert(ToneGenerator::kAmplitude, NodeValue(NodeValue::kFloat, 0.0));
  NodeValueTable silent_table;
  tone.Value(silent_row, globals, &silent_table);
  SampleBuffer silent_buf = silent_table.Get(NodeValue::kSamples).toSamples();
  for (int i = 0, n = static_cast<int>(silent_buf.sample_count()); i < n; ++i) {
    OLIVE_ASSERT(std::fabs(silent_buf.data(0)[i]) < 1e-9f);
  }

  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(MergeNodeAlphaPreservation)
{
  MergeNode merge;
  const VideoParams params(32, 32, PixelFormat::F32, 4);

  // Verify the shader code implements alpha-over blending
  Node::ShaderRequest req(QStringLiteral("alphaover"));
  ShaderCode code = merge.GetShaderCode(req);
  OLIVE_ASSERT(!code.frag_code().isEmpty());
  // alphaover.frag must handle alpha channel
  OLIVE_ASSERT(code.frag_code().contains(QStringLiteral("alpha")) ||
               code.frag_code().contains(QStringLiteral(".a")));

  // With both base and blend textures: output must not be null
  auto base_tex = std::make_shared<Texture>(params);
  auto blend_tex = std::make_shared<Texture>(params);
  NodeGlobals globals(params, AudioParams(), TimeRange(0, 1), LoopMode::kLoopModeOff);
  NodeValueTable table;
  NodeValueRow row;
  row.insert(MergeNode::kBaseIn, NodeValue(NodeValue::kTexture, base_tex));
  row.insert(MergeNode::kBlendIn, NodeValue(NodeValue::kTexture, blend_tex));
  merge.Value(row, globals, &table);

  TexturePtr out = table.Get(NodeValue::kTexture).toTexture();
  OLIVE_ASSERT(out != nullptr);

  OLIVE_TEST_END;
}

}
