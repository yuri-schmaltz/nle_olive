#include <algorithm>
#include <cmath>
#include <vector>

#include <libavutil/channel_layout.h>

#include "testutil.h"
#include <olive/core/core.h>
#include "node/audio/equalizer/equalizer.h"
#include "node/audio/equalizer/biquad.h"

namespace olive {

namespace {

const int kSampleRate = 48000;
const size_t kTestSampleCount = 4800; // 0.1 seconds

AudioParams TestStereoParams() {
  return AudioParams(kSampleRate, uint64_t(AV_CH_LAYOUT_STEREO), core::SampleFormat::F32P);
}

void FillSine(SampleBuffer *sb, double frequency, double amplitude = 1.0) {
  sb->silence();
  const size_t n = sb->sample_count();
  const int channels = sb->channel_count();
  const double fs = double(sb->audio_params().sample_rate());
  constexpr double kPi = 3.14159265358979323846;
  const double w = 2.0 * kPi * frequency / fs;

  for (size_t i = 0; i < n; i++) {
    const float val = float(amplitude * std::sin(w * double(i)));
    for (int c = 0; c < channels; c++) {
      sb->data(c)[i] = val;
    }
  }
}

float CalculateRMS(const float *samples, size_t start_index, size_t count) {
  double sum_sq = 0.0;
  for (size_t i = start_index; i < start_index + count; i++) {
    sum_sq += double(samples[i]) * double(samples[i]);
  }
  return float(std::sqrt(sum_sq / double(count)));
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: Flat Response Verification (Bypass & 0 dB gain)
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(EqualizerFlatResponse)
{
  const AudioParams params = TestStereoParams();
  SampleBuffer input(params, kTestSampleCount);
  FillSine(&input, 1000.0, 1.0);

  // Case A: Enabled = false (Bypass)
  {
    EqualizerNode eq;
    eq.SetStandardValue(EqualizerNode::kEnabledInput, false);

    NodeValueRow row;
    row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
    row.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, false));

    NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(kTestSampleCount, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table;
    eq.Value(row, globals, &table);

    SampleBuffer output = table.Get(NodeValue::kSamples).toSamples();
    OLIVE_ASSERT_EQUAL(output.sample_count(), input.sample_count());
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kTestSampleCount; i++) {
        OLIVE_ASSERT(std::fabs(output.data(c)[i] - input.data(c)[i]) < 1e-5f);
      }
    }
  }

  // Case B: Enabled = true, all bands at 0 dB gain
  {
    EqualizerNode eq;
    eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
    for (int b = 0; b < EqualizerNode::kBandCount; b++) {
      eq.SetBandGain(b, 0.0f);
    }

    NodeValueRow row;
    row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
    row.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));

    NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(kTestSampleCount, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table;
    eq.Value(row, globals, &table);

    SampleBuffer output = table.Get(NodeValue::kSamples).toSamples();
    OLIVE_ASSERT_EQUAL(output.sample_count(), input.sample_count());
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kTestSampleCount; i++) {
        OLIVE_ASSERT(std::fabs(output.data(c)[i] - input.data(c)[i]) < 1e-5f);
      }
    }
  }

  OLIVE_TEST_END;
}

// ---------------------------------------------------------------------------
// Test 2: Filter Response Verification (Low-pass, High-pass, Peaking Bell)
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(EqualizerFilterResponses)
{
  const AudioParams params = TestStereoParams();

  // Part 1: Low-Pass Filter (Cutoff = 1000 Hz, Q = 0.707)
  {
    EqualizerNode eq;
    eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
    eq.SetBandType(0, kFilterLowPass);
    eq.SetBandFrequency(0, 1000.0f);
    eq.SetBandQ(0, 0.7071f);
    for (int b = 1; b < EqualizerNode::kBandCount; b++) eq.SetBandEnabled(b, false);

    // 100 Hz tone (Passband)
    SampleBuffer low_in(params, kTestSampleCount);
    FillSine(&low_in, 100.0, 1.0);
    NodeValueRow row_low;
    row_low.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, low_in));
    row_low.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeGlobals globals_low(VideoParams(), params, TimeRange(0, rational(kTestSampleCount, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table_low;
    eq.Value(row_low, globals_low, &table_low);
    SampleBuffer low_out = table_low.Get(NodeValue::kSamples).toSamples();

    // 10000 Hz tone (Stopband)
    SampleBuffer high_in(params, kTestSampleCount);
    FillSine(&high_in, 10000.0, 1.0);
    NodeValueRow row_high;
    row_high.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, high_in));
    row_high.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeGlobals globals_high(VideoParams(), params, TimeRange(0, rational(kTestSampleCount, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table_high;
    eq.Value(row_high, globals_high, &table_high);
    SampleBuffer high_out = table_high.Get(NodeValue::kSamples).toSamples();

    float low_rms = CalculateRMS(low_out.data(0), 1000, 3800);
    float high_rms = CalculateRMS(high_out.data(0), 1000, 3800);

    OLIVE_ASSERT(low_rms > 0.65f);
    OLIVE_ASSERT(high_rms < 0.025f);
  }

  // Part 2: High-Pass Filter (Cutoff = 1000 Hz, Q = 0.707)
  {
    EqualizerNode eq;
    eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
    eq.SetBandType(0, kFilterHighPass);
    eq.SetBandFrequency(0, 1000.0f);
    eq.SetBandQ(0, 0.7071f);
    for (int b = 1; b < EqualizerNode::kBandCount; b++) eq.SetBandEnabled(b, false);

    // 100 Hz tone (Stopband)
    SampleBuffer low_in(params, kTestSampleCount);
    FillSine(&low_in, 100.0, 1.0);
    NodeValueRow row_low;
    row_low.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, low_in));
    row_low.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeGlobals globals_low(VideoParams(), params, TimeRange(0, rational(kTestSampleCount, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table_low;
    eq.Value(row_low, globals_low, &table_low);
    SampleBuffer low_out = table_low.Get(NodeValue::kSamples).toSamples();

    // 5000 Hz tone (Passband)
    SampleBuffer high_in(params, kTestSampleCount);
    FillSine(&high_in, 5000.0, 1.0);
    NodeValueRow row_high;
    row_high.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, high_in));
    row_high.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeGlobals globals_high(VideoParams(), params, TimeRange(0, rational(kTestSampleCount, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table_high;
    eq.Value(row_high, globals_high, &table_high);
    SampleBuffer high_out = table_high.Get(NodeValue::kSamples).toSamples();

    float low_rms = CalculateRMS(low_out.data(0), 1000, 3800);
    float high_rms = CalculateRMS(high_out.data(0), 1000, 3800);

    OLIVE_ASSERT(low_rms < 0.025f);
    OLIVE_ASSERT(high_rms > 0.65f);
  }

  // Part 3: Peaking Bell Filter (+6.02 dB boost = factor 2.0x, -6.02 dB cut = factor 0.5x)
  {
    // +6 dB boost at 1000 Hz
    EqualizerNode eq_boost;
    eq_boost.SetStandardValue(EqualizerNode::kEnabledInput, true);
    eq_boost.SetBandType(0, kFilterPeaking);
    eq_boost.SetBandFrequency(0, 1000.0f);
    eq_boost.SetBandGain(0, 6.0206f);
    eq_boost.SetBandQ(0, 2.0f);
    for (int b = 1; b < EqualizerNode::kBandCount; b++) eq_boost.SetBandEnabled(b, false);

    SampleBuffer target_in(params, kTestSampleCount);
    FillSine(&target_in, 1000.0, 1.0);
    NodeValueRow row_b;
    row_b.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, target_in));
    row_b.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeGlobals globals_b(VideoParams(), params, TimeRange(0, rational(kTestSampleCount, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table_b;
    eq_boost.Value(row_b, globals_b, &table_b);
    SampleBuffer boost_out = table_b.Get(NodeValue::kSamples).toSamples();

    float boosted_rms = CalculateRMS(boost_out.data(0), 1000, 3800);
    OLIVE_ASSERT(std::fabs(boosted_rms - 1.4142f) < 0.07f);

    // Far-off frequency (100 Hz) should stay unity gain (~0.7071)
    SampleBuffer far_in(params, kTestSampleCount);
    FillSine(&far_in, 100.0, 1.0);
    NodeValueRow row_far;
    row_far.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, far_in));
    row_far.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeValueTable table_far;
    eq_boost.Value(row_far, globals_b, &table_far);
    SampleBuffer far_out = table_far.Get(NodeValue::kSamples).toSamples();
    float far_rms = CalculateRMS(far_out.data(0), 1000, 3800);
    OLIVE_ASSERT(std::fabs(far_rms - 0.7071f) < 0.05f);

    // -6 dB cut at 1000 Hz
    EqualizerNode eq_cut;
    eq_cut.SetStandardValue(EqualizerNode::kEnabledInput, true);
    eq_cut.SetBandType(0, kFilterPeaking);
    eq_cut.SetBandFrequency(0, 1000.0f);
    eq_cut.SetBandGain(0, -6.0206f);
    eq_cut.SetBandQ(0, 2.0f);
    for (int b = 1; b < EqualizerNode::kBandCount; b++) eq_cut.SetBandEnabled(b, false);

    NodeValueTable table_cut;
    eq_cut.Value(row_b, globals_b, &table_cut);
    SampleBuffer cut_out = table_cut.Get(NodeValue::kSamples).toSamples();
    float cut_rms = CalculateRMS(cut_out.data(0), 1000, 3800);
    OLIVE_ASSERT(std::fabs(cut_rms - 0.3535f) < 0.04f);
  }

  OLIVE_TEST_END;
}

// ---------------------------------------------------------------------------
// Test 3: Stability Verification with Unit Impulse (48,000 samples)
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(EqualizerUnitImpulseStability)
{
  const AudioParams params = TestStereoParams();
  const size_t kOneSecond = 48000;
  SampleBuffer impulse(params, kOneSecond);
  impulse.silence();
  impulse.data(0)[0] = 1.0f;
  impulse.data(1)[0] = 1.0f;

  EqualizerNode eq;
  eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
  eq.SetBandType(0, kFilterPeaking);
  eq.SetBandFrequency(0, 1000.0f);
  eq.SetBandGain(0, 18.0f);
  eq.SetBandQ(0, 5.0f);

  eq.SetBandType(1, kFilterHighShelf);
  eq.SetBandFrequency(1, 8000.0f);
  eq.SetBandGain(1, 12.0f);
  eq.SetBandQ(1, 1.0f);

  NodeValueRow row;
  row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, impulse));
  row.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
  NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(kOneSecond, kSampleRate)), LoopMode::kLoopModeOff);
  NodeValueTable table;
  eq.Value(row, globals, &table);

  SampleBuffer response = table.Get(NodeValue::kSamples).toSamples();
  OLIVE_ASSERT_EQUAL(response.sample_count(), kOneSecond);

  for (int c = 0; c < 2; c++) {
    const float *data = response.data(c);
    for (size_t i = 0; i < kOneSecond; i++) {
      const float val = data[i];
      OLIVE_ASSERT(std::isfinite(val));
      OLIVE_ASSERT(std::fabs(val) < 20.0f);
    }

    for (size_t i = 4800; i < kOneSecond; i++) {
      OLIVE_ASSERT(std::fabs(data[i]) < 1e-3f);
    }

    OLIVE_ASSERT(std::fabs(data[kOneSecond - 1]) < 1e-6f);
  }

  OLIVE_TEST_END;
}

// ---------------------------------------------------------------------------
// Test 4: Nyquist & Parameter Limit Boundary Cases
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(EqualizerNyquistAndLimits)
{
  const AudioParams params = TestStereoParams();
  SampleBuffer input(params, 1024);
  FillSine(&input, 1000.0, 1.0);

  // Test Boundary 1: Frequency near 0 Hz (clamped to 10 Hz minimum)
  {
    EqualizerNode eq;
    eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
    eq.SetBandFrequency(0, 0.0f);
    eq.SetBandGain(0, 12.0f);
    eq.SetBandQ(0, 0.707f);

    NodeValueRow row;
    row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
    row.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(1024, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table;
    eq.Value(row, globals, &table);

    SampleBuffer out = table.Get(NodeValue::kSamples).toSamples();
    OLIVE_ASSERT_EQUAL(out.sample_count(), size_t(1024));
    for (size_t i = 0; i < 1024; i++) {
      OLIVE_ASSERT(std::isfinite(out.data(0)[i]));
    }
  }

  // Test Boundary 2: Frequency at Nyquist (24,000 Hz, clamped to 0.49 * Fs = 23,520 Hz)
  {
    EqualizerNode eq;
    eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
    eq.SetBandFrequency(0, 24000.0f);
    eq.SetBandGain(0, -24.0f);
    eq.SetBandQ(0, 10.0f);

    NodeValueRow row;
    row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
    row.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(1024, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table;
    eq.Value(row, globals, &table);

    SampleBuffer out = table.Get(NodeValue::kSamples).toSamples();
    OLIVE_ASSERT_EQUAL(out.sample_count(), size_t(1024));
    for (size_t i = 0; i < 1024; i++) {
      OLIVE_ASSERT(std::isfinite(out.data(0)[i]));
    }
  }

  // Test Boundary 3: Extreme Q (0.001 and 100.0) clamped to safe limits
  {
    EqualizerNode eq;
    eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
    eq.SetBandFrequency(0, 1000.0f);
    eq.SetBandGain(0, 6.0f);
    eq.SetBandQ(0, 0.001f);

    NodeValueRow row;
    row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
    row.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(1024, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table;
    eq.Value(row, globals, &table);

    SampleBuffer out = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < 1024; i++) {
      OLIVE_ASSERT(std::isfinite(out.data(0)[i]));
    }
  }

  OLIVE_TEST_END;
}

// ---------------------------------------------------------------------------
// Test 5: Multi-Channel Planar Processing (Stereo Buffer Independence)
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(EqualizerPlanarStereoIndependence)
{
  const AudioParams params = TestStereoParams();
  SampleBuffer input(params, kTestSampleCount);
  input.silence();

  // Channel 0: 100 Hz tone (low frequency)
  const double fs = double(kSampleRate);
  constexpr double kPi = 3.14159265358979323846;
  const double w0 = 2.0 * kPi * 100.0 / fs;
  for (size_t i = 0; i < kTestSampleCount; i++) {
    input.data(0)[i] = float(std::sin(w0 * double(i)));
  }

  // Channel 1: 10,000 Hz tone (high frequency)
  const double w1 = 2.0 * kPi * 10000.0 / fs;
  for (size_t i = 0; i < kTestSampleCount; i++) {
    input.data(1)[i] = float(std::sin(w1 * double(i)));
  }

  EqualizerNode eq;
  eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
  eq.SetBandType(0, kFilterLowPass);
  eq.SetBandFrequency(0, 1000.0f);
  eq.SetBandQ(0, 0.7071f);
  for (int b = 1; b < EqualizerNode::kBandCount; b++) eq.SetBandEnabled(b, false);

  NodeValueRow row;
  row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
  row.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
  NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(kTestSampleCount, kSampleRate)), LoopMode::kLoopModeOff);
  NodeValueTable table;
  eq.Value(row, globals, &table);

  SampleBuffer output = table.Get(NodeValue::kSamples).toSamples();

  float ch0_rms = CalculateRMS(output.data(0), 1000, 3800);
  float ch1_rms = CalculateRMS(output.data(1), 1000, 3800);

  OLIVE_ASSERT(ch0_rms > 0.65f);
  OLIVE_ASSERT(ch1_rms < 0.025f);

  OLIVE_TEST_END;
}

} // namespace olive
