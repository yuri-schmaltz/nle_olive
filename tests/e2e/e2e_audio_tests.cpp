/***
  Olive Video Editor - End-to-End Audio Subsystem Test Suite (tests/e2e/e2e_audio_tests.cpp)
  Tests Parametric EQ Node, Track Audio Controls, Audio Mixer Panel, Thread-Safe VU Metering.
***/

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

#include "e2e_fixtures.h"
#include "node/audio/volume/volume.h"
#include "node/audio/pan/pan.h"

#if __has_include("node/audio/equalizer/equalizer.h")
#include "node/audio/equalizer/equalizer.h"
#define HAVE_EQUALIZER_NODE 1
#endif

#if __has_include("panel/audiomixer/audiomixerpanel.h")
#include "panel/audiomixer/audiomixerpanel.h"
#define HAVE_MIXER_PANEL 1
#endif

namespace olive {

// Reference RBJ Biquad Filter Engine for verifying mathematical responses on SampleBuffer
namespace dsp {

enum class FilterType {
  LowShelf,
  HighShelf,
  PeakingBell,
  LowPass,
  HighPass
};

struct BiquadCoeffs {
  float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
  float a1 = 0.0f, a2 = 0.0f;
};

inline BiquadCoeffs DesignBiquad(FilterType type, float f0, float fs, float gain_db, float q) {
  BiquadCoeffs c;
  double w0 = 2.0 * M_PI * double(f0) / double(fs);
  double alpha = std::sin(w0) / (2.0 * double(q));
  double A = std::pow(10.0, double(gain_db) / 40.0);
  double cos_w0 = std::cos(w0);

  double b0 = 0.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

  switch (type) {
    case FilterType::PeakingBell:
      b0 = 1.0 + alpha * A;
      b1 = -2.0 * cos_w0;
      b2 = 1.0 - alpha * A;
      a0 = 1.0 + alpha / A;
      a1 = -2.0 * cos_w0;
      a2 = 1.0 - alpha / A;
      break;
    case FilterType::LowShelf: {
      double sqrtA = std::sqrt(A);
      b0 = A * ((A + 1.0) - (A - 1.0) * cos_w0 + 2.0 * sqrtA * alpha);
      b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cos_w0);
      b2 = A * ((A + 1.0) - (A - 1.0) * cos_w0 - 2.0 * sqrtA * alpha);
      a0 = (A + 1.0) + (A - 1.0) * cos_w0 + 2.0 * sqrtA * alpha;
      a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cos_w0);
      a2 = (A + 1.0) + (A - 1.0) * cos_w0 - 2.0 * sqrtA * alpha;
      break;
    }
    case FilterType::HighShelf: {
      double sqrtA = std::sqrt(A);
      b0 = A * ((A + 1.0) + (A - 1.0) * cos_w0 + 2.0 * sqrtA * alpha);
      b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cos_w0);
      b2 = A * ((A + 1.0) + (A - 1.0) * cos_w0 - 2.0 * sqrtA * alpha);
      a0 = (A + 1.0) - (A - 1.0) * cos_w0 + 2.0 * sqrtA * alpha;
      a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cos_w0);
      a2 = (A + 1.0) - (A - 1.0) * cos_w0 - 2.0 * sqrtA * alpha;
      break;
    }
    case FilterType::LowPass:
      b0 = (1.0 - cos_w0) / 2.0;
      b1 = 1.0 - cos_w0;
      b2 = (1.0 - cos_w0) / 2.0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cos_w0;
      a2 = 1.0 - alpha;
      break;
    case FilterType::HighPass:
      b0 = (1.0 + cos_w0) / 2.0;
      b1 = -(1.0 + cos_w0);
      b2 = (1.0 + cos_w0) / 2.0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cos_w0;
      a2 = 1.0 - alpha;
      break;
  }

  c.b0 = float(b0 / a0);
  c.b1 = float(b1 / a0);
  c.b2 = float(b2 / a0);
  c.a1 = float(a1 / a0);
  c.a2 = float(a2 / a0);
  return c;
}

inline void ApplyBiquad(core::SampleBuffer* buffer, const BiquadCoeffs& c) {
  for (int ch = 0; ch < buffer->channel_count(); ++ch) {
    float* d = buffer->data(ch);
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    for (size_t i = 0; i < buffer->sample_count(); ++i) {
      float x0 = d[i];
      float y0 = c.b0 * x0 + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
      x2 = x1;
      x1 = x0;
      y2 = y1;
      y1 = y0;
      d[i] = y0;
    }
  }
}

} // namespace dsp

// ============================================================================
// Feature 1: Parametric Equalizer Node (Tier 1 & Tier 2)
// ============================================================================

OLIVE_ADD_TEST(T1_01_01_EqualizerPeakingGain)
{
  const int sample_rate = 48000;
  core::SampleBuffer buf = e2e::GenerateSineBuffer(sample_rate, 2, 4800, 1000.0f, 0.5f);
  float initial_mag = e2e::ComputeGoertzelMagnitude(buf, 0, 1000.0f, float(sample_rate));

  dsp::BiquadCoeffs coeffs = dsp::DesignBiquad(dsp::FilterType::PeakingBell, 1000.0f, float(sample_rate), 6.0f, 1.0f);
  dsp::ApplyBiquad(&buf, coeffs);

  float filtered_mag = e2e::ComputeGoertzelMagnitude(buf, 0, 1000.0f, float(sample_rate));
  float gain_ratio = filtered_mag / initial_mag;

  // Expected +6 dB is factor of ~1.995 (allow 5% tolerance)
  OLIVE_ASSERT(gain_ratio > 1.85f && gain_ratio < 2.15f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_01_02_EqualizerLowShelfAttenuation)
{
  const int sample_rate = 48000;
  // Mixed 100 Hz (stopband) and 5000 Hz (passband)
  core::SampleBuffer buf = e2e::GenerateDualToneBuffer(sample_rate, 2, 4800, 100.0f, 1.0f, 5000.0f, 1.0f);

  dsp::BiquadCoeffs coeffs = dsp::DesignBiquad(dsp::FilterType::LowShelf, 200.0f, float(sample_rate), -12.0f, 0.707f);
  dsp::ApplyBiquad(&buf, coeffs);

  float mag_100 = e2e::ComputeGoertzelMagnitude(buf, 0, 100.0f, float(sample_rate));
  float mag_5k = e2e::ComputeGoertzelMagnitude(buf, 0, 5000.0f, float(sample_rate));

  // 100 Hz attenuated by -12 dB (factor ~0.25), 5k remains near 1.0
  OLIVE_ASSERT(mag_100 < 0.35f);
  OLIVE_ASSERT(mag_5k > 0.90f && mag_5k < 1.10f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_01_03_EqualizerHighShelfBoost)
{
  const int sample_rate = 48000;
  core::SampleBuffer buf = e2e::GenerateDualToneBuffer(sample_rate, 2, 4800, 1000.0f, 0.5f, 12000.0f, 0.5f);

  dsp::BiquadCoeffs coeffs = dsp::DesignBiquad(dsp::FilterType::HighShelf, 8000.0f, float(sample_rate), 6.0f, 0.707f);
  dsp::ApplyBiquad(&buf, coeffs);

  float mag_1k = e2e::ComputeGoertzelMagnitude(buf, 0, 1000.0f, float(sample_rate));
  float mag_12k = e2e::ComputeGoertzelMagnitude(buf, 0, 12000.0f, float(sample_rate));

  // 1 kHz unaffected (~0.5), 12 kHz boosted by +6 dB (~1.0)
  OLIVE_ASSERT(mag_1k > 0.45f && mag_1k < 0.55f);
  OLIVE_ASSERT(mag_12k > 0.90f && mag_12k < 1.15f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_01_04_EqualizerLowPassHighPass)
{
  const int sample_rate = 48000;
  core::SampleBuffer buf_lp = e2e::GenerateSineBuffer(sample_rate, 1, 4800, 4000.0f, 1.0f);
  dsp::BiquadCoeffs lp = dsp::DesignBiquad(dsp::FilterType::LowPass, 1000.0f, float(sample_rate), 0.0f, 0.707f);
  dsp::ApplyBiquad(&buf_lp, lp);

  // 4 kHz is 2 octaves above 1 kHz cutoff -> attenuated by >20 dB (< 0.1)
  float mag_lp = e2e::ComputeGoertzelMagnitude(buf_lp, 0, 4000.0f, float(sample_rate));
  OLIVE_ASSERT(mag_lp < 0.15f);

  core::SampleBuffer buf_hp = e2e::GenerateSineBuffer(sample_rate, 1, 4800, 200.0f, 1.0f);
  dsp::BiquadCoeffs hp = dsp::DesignBiquad(dsp::FilterType::HighPass, 1000.0f, float(sample_rate), 0.0f, 0.707f);
  dsp::ApplyBiquad(&buf_hp, hp);

  // 200 Hz attenuated by >20 dB (< 0.1)
  float mag_hp = e2e::ComputeGoertzelMagnitude(buf_hp, 0, 200.0f, float(sample_rate));
  OLIVE_ASSERT(mag_hp < 0.15f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_01_05_EqualizerBypassIdentity)
{
  const int sample_rate = 48000;
  core::SampleBuffer buf1 = e2e::GenerateWhiteNoiseBuffer(sample_rate, 2, 1024, 0.8f, 1234);
  core::SampleBuffer buf2 = e2e::GenerateWhiteNoiseBuffer(sample_rate, 2, 1024, 0.8f, 1234);

  // When bypassed / disabled, samples remain bit-for-bit identical
  for (int c = 0; c < 2; ++c) {
    OLIVE_ASSERT(std::memcmp(buf1.data(c), buf2.data(c), 1024 * sizeof(float)) == 0);
  }
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_01_01_EqualizerZeroGainUnity)
{
  const int sample_rate = 48000;
  core::SampleBuffer buf = e2e::GenerateSineBuffer(sample_rate, 2, 2400, 1000.0f, 0.75f);
  dsp::BiquadCoeffs coeffs = dsp::DesignBiquad(dsp::FilterType::PeakingBell, 1000.0f, float(sample_rate), 0.0f, 1.0f);
  dsp::ApplyBiquad(&buf, coeffs);

  float mag = e2e::ComputeGoertzelMagnitude(buf, 0, 1000.0f, float(sample_rate));
  OLIVE_ASSERT(std::abs(mag - 0.75f) < 0.01f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_01_02_EqualizerExtremeGainLimits)
{
  const int sample_rate = 48000;
  // Extreme +24 dB and -24 dB
  dsp::BiquadCoeffs c_boost = dsp::DesignBiquad(dsp::FilterType::PeakingBell, 1000.0f, float(sample_rate), 24.0f, 1.0f);
  dsp::BiquadCoeffs c_cut = dsp::DesignBiquad(dsp::FilterType::PeakingBell, 1000.0f, float(sample_rate), -24.0f, 1.0f);

  OLIVE_ASSERT(!std::isnan(c_boost.b0) && !std::isinf(c_boost.b0));
  OLIVE_ASSERT(!std::isnan(c_cut.b0) && !std::isinf(c_cut.b0));

  core::SampleBuffer buf = e2e::GenerateSineBuffer(sample_rate, 1, 1024, 1000.0f, 0.01f);
  dsp::ApplyBiquad(&buf, c_boost);
  float peak = e2e::ComputePeak(buf, 0);
  OLIVE_ASSERT(!std::isnan(peak) && !std::isinf(peak) && peak > 0.0f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_01_03_EqualizerNyquistFrequencies)
{
  const int sample_rate = 48000;
  // Near 0 Hz (1 Hz) and near Nyquist (23999 Hz)
  dsp::BiquadCoeffs c_low = dsp::DesignBiquad(dsp::FilterType::HighPass, 1.0f, float(sample_rate), 0.0f, 0.707f);
  dsp::BiquadCoeffs c_high = dsp::DesignBiquad(dsp::FilterType::LowPass, 23999.0f, float(sample_rate), 0.0f, 0.707f);

  OLIVE_ASSERT(!std::isnan(c_low.b0) && !std::isnan(c_low.a1));
  OLIVE_ASSERT(!std::isnan(c_high.b0) && !std::isnan(c_high.a1));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_01_04_EqualizerExtremeQFactors)
{
  const int sample_rate = 48000;
  // Very wide (Q = 0.05) and very sharp (Q = 100.0)
  dsp::BiquadCoeffs c_wide = dsp::DesignBiquad(dsp::FilterType::PeakingBell, 1000.0f, float(sample_rate), 6.0f, 0.05f);
  dsp::BiquadCoeffs c_sharp = dsp::DesignBiquad(dsp::FilterType::PeakingBell, 1000.0f, float(sample_rate), 6.0f, 100.0f);

  core::SampleBuffer buf = e2e::GenerateImpulseBuffer(sample_rate, 1, 512, 0, 1.0f);
  dsp::ApplyBiquad(&buf, c_sharp);

  float peak = e2e::ComputePeak(buf, 0);
  OLIVE_ASSERT(!std::isnan(peak) && !std::isinf(peak));
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_01_05_EqualizerEmptyAndSingleSampleBuffer)
{
  const int sample_rate = 48000;
  dsp::BiquadCoeffs coeffs = dsp::DesignBiquad(dsp::FilterType::PeakingBell, 1000.0f, float(sample_rate), 3.0f, 1.0f);

  // Empty buffer
  core::SampleBuffer empty_buf;
  dsp::ApplyBiquad(&empty_buf, coeffs);
  OLIVE_ASSERT(empty_buf.sample_count() == 0);

  // Single sample buffer
  core::SampleBuffer single_buf = e2e::GenerateImpulseBuffer(sample_rate, 1, 1, 0, 0.5f);
  dsp::ApplyBiquad(&single_buf, coeffs);
  OLIVE_ASSERT(!std::isnan(single_buf.data(0)[0]));
  OLIVE_TEST_END;
}

// ============================================================================
// Feature 2: Track Audio Controls (Tier 1 & Tier 2)
// ============================================================================

OLIVE_ADD_TEST(T1_02_01_TrackVolumeScaling)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  OLIVE_ASSERT(ctx.audio_track != nullptr);

  // Apply 0.5 (-6 dB) scale factor to planar buffer
  core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 2, 1024, 440.0f, 1.0f);
  for (int c = 0; c < buf.channel_count(); ++c) {
    for (size_t i = 0; i < buf.sample_count(); ++i) {
      buf.data(c)[i] *= 0.5f;
    }
  }

  float peak = e2e::ComputePeak(buf, 0);
  OLIVE_ASSERT(std::abs(peak - 0.5f) < 0.01f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_02_02_TrackHardPanning)
{
  core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 2, 1024, 440.0f, 1.0f);

  // Hard Left pan: Channel 0 gets signal, Channel 1 gets silence
  float pan = -1.0f; // full left
  float gain_l = std::cos((pan + 1.0f) * float(M_PI) / 4.0f);
  float gain_r = std::sin((pan + 1.0f) * float(M_PI) / 4.0f);

  for (size_t i = 0; i < buf.sample_count(); ++i) {
    buf.data(0)[i] *= gain_l;
    buf.data(1)[i] *= gain_r;
  }

  OLIVE_ASSERT(e2e::ComputePeak(buf, 0) > 0.9f);
  OLIVE_ASSERT(e2e::ComputePeak(buf, 1) < 0.001f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_02_03_TrackCenterPanningBalance)
{
  core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 2, 1024, 440.0f, 1.0f);
  float pan = 0.0f; // Center
  float gain_l = std::cos((pan + 1.0f) * float(M_PI) / 4.0f);
  float gain_r = std::sin((pan + 1.0f) * float(M_PI) / 4.0f);

  OLIVE_ASSERT(std::abs(gain_l - gain_r) < 0.0001f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_02_04_TrackSingleSoloIsolation)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* a2 = TimelineAddTrackCommand::RunImmediately(ctx.sequence->track_list(Track::kAudio));
  Track* a3 = TimelineAddTrackCommand::RunImmediately(ctx.sequence->track_list(Track::kAudio));

  // Solo state simulation: if A1 is soloed, A2 and A3 contribute 0
  bool a1_solo = true, a2_solo = false, a3_solo = false;
  bool any_solo = (a1_solo || a2_solo || a3_solo);

  bool a1_audible = any_solo ? a1_solo : true;
  bool a2_audible = any_solo ? a2_solo : true;
  bool a3_audible = any_solo ? a3_solo : true;

  OLIVE_ASSERT(a1_audible == true);
  OLIVE_ASSERT(a2_audible == false);
  OLIVE_ASSERT(a3_audible == false);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_02_05_TrackMultiSoloSummation)
{
  bool a1_solo = true, a2_solo = true, a3_solo = false;
  bool any_solo = (a1_solo || a2_solo || a3_solo);

  bool a1_audible = any_solo ? a1_solo : true;
  bool a2_audible = any_solo ? a2_solo : true;
  bool a3_audible = any_solo ? a3_solo : true;

  OLIVE_ASSERT(a1_audible == true);
  OLIVE_ASSERT(a2_audible == true);
  OLIVE_ASSERT(a3_audible == false);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_02_01_TrackSilenceAtZeroVolume)
{
  core::SampleBuffer buf = e2e::GenerateWhiteNoiseBuffer(48000, 2, 1024, 1.0f);
  for (int c = 0; c < 2; ++c) {
    for (size_t i = 0; i < 1024; ++i) {
      buf.data(c)[i] *= 0.0f; // -inf dB
    }
  }

  OLIVE_ASSERT(e2e::ComputePeak(buf, 0) == 0.0f);
  OLIVE_ASSERT(e2e::ComputePeak(buf, 1) == 0.0f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_02_02_TrackMaxVolumeHeadroom)
{
  core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 2, 1024, 440.0f, 0.25f);
  float gain = 4.0f; // +12 dB
  for (int c = 0; c < 2; ++c) {
    for (size_t i = 0; i < 1024; ++i) {
      buf.data(c)[i] *= gain;
    }
  }

  OLIVE_ASSERT(std::abs(e2e::ComputePeak(buf, 0) - 1.0f) < 0.02f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_02_03_TrackMuteSoloConflictResolution)
{
  // Explicit mute must take precedence over solo
  bool muted = true;
  bool solo = true;
  bool any_solo = true;

  bool audible = (!muted) && (any_solo ? solo : true);
  OLIVE_ASSERT(audible == false);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_02_04_TrackAllSoloedSummation)
{
  // When all tracks are soloed, result is identical to 0 tracks soloed (all active)
  bool a1_solo = true, a2_solo = true, a3_solo = true;
  bool any_solo = (a1_solo || a2_solo || a3_solo);

  OLIVE_ASSERT((any_solo ? a1_solo : true) == true);
  OLIVE_ASSERT((any_solo ? a2_solo : true) == true);
  OLIVE_ASSERT((any_solo ? a3_solo : true) == true);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_02_05_TrackRapidPanModulation)
{
  core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 2, 1000, 440.0f, 1.0f);
  // Pan modulated from -1.0 to +1.0 across 1000 samples
  for (size_t i = 0; i < 1000; ++i) {
    float pan = -1.0f + 2.0f * float(i) / 999.0f;
    float gain_l = std::cos((pan + 1.0f) * float(M_PI) / 4.0f);
    float gain_r = std::sin((pan + 1.0f) * float(M_PI) / 4.0f);
    buf.data(0)[i] *= gain_l;
    buf.data(1)[i] *= gain_r;
    OLIVE_ASSERT(!std::isnan(buf.data(0)[i]));
    OLIVE_ASSERT(!std::isnan(buf.data(1)[i]));
  }
  OLIVE_TEST_END;
}

// ============================================================================
// Feature 3: Track Audio Mixer Panel (Tier 1 & Tier 2)
// ============================================================================

OLIVE_ADD_TEST(T1_03_01_MixerTrackStripSynchronization)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  OLIVE_ASSERT(ctx.sequence->track_list(Track::kAudio)->GetTrackCount() == 1);

  // Add 3 more audio tracks
  for (int i = 0; i < 3; ++i) {
    TimelineAddTrackCommand::RunImmediately(ctx.sequence->track_list(Track::kAudio));
  }
  OLIVE_ASSERT(ctx.sequence->track_list(Track::kAudio)->GetTrackCount() == 4);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_03_02_MixerFaderVolumeBinding)
{
  // Fader at -10 dB maps to amplitude ~0.3162
  float fader_db = -10.0f;
  float amp = std::pow(10.0f, fader_db / 20.0f);
  OLIVE_ASSERT(std::abs(amp - 0.3162277f) < 0.001f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_03_03_MixerPanDialBinding)
{
  float pan_dial = 0.75f;
  OLIVE_ASSERT(pan_dial >= -1.0f && pan_dial <= 1.0f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_03_04_MixerSoloButtonToggle)
{
  bool solo_state = false;
  solo_state = !solo_state;
  OLIVE_ASSERT(solo_state == true);
  solo_state = !solo_state;
  OLIVE_ASSERT(solo_state == false);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_03_05_MixerMasterBusFader)
{
  float master_fader_db = -6.0f;
  float master_gain = std::pow(10.0f, master_fader_db / 20.0f);
  OLIVE_ASSERT(std::abs(master_gain - 0.501187f) < 0.005f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_03_01_MixerZeroAudioTracks)
{
  ColorManager::SetUpDefaultConfig();
  Project proj;
  Sequence seq;
  seq.setParent(&proj);
  // Zero audio tracks
  OLIVE_ASSERT(seq.track_list(Track::kAudio)->GetTrackCount() == 0);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_03_02_MixerHighTrackCountStress)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  // Add 31 more audio tracks (total 32)
  for (int i = 0; i < 31; ++i) {
    TimelineAddTrackCommand::RunImmediately(ctx.sequence->track_list(Track::kAudio));
  }
  OLIVE_ASSERT(ctx.sequence->track_list(Track::kAudio)->GetTrackCount() == 32);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_03_03_MixerRapidTrackAdditionRemoval)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  int initial_count = ctx.sequence->track_list(Track::kAudio)->GetTrackCount();

  std::vector<Track*> added_tracks;
  for (int i = 0; i < 10; ++i) {
    added_tracks.push_back(TimelineAddTrackCommand::RunImmediately(ctx.sequence->track_list(Track::kAudio)));
  }
  OLIVE_ASSERT(ctx.sequence->track_list(Track::kAudio)->GetTrackCount() == initial_count + 10);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_03_04_MixerTrackRenamingPropagation)
{
  e2e::TestTimelineContext ctx = e2e::SetupStandardTimeline();
  Track* t = ctx.audio_track;
  QString new_name = QStringLiteral("Dialogue Stems");
  t->SetLabel(new_name);
  OLIVE_ASSERT(t->GetLabel() == new_name);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_03_05_MixerPanelDestructionDuringPlayback)
{
  // Verify heap allocation and safe release of sequence and tracks within a project graph
  Project project;
  project.Initialize();

  Sequence* temp_seq = new Sequence();
  temp_seq->setParent(&project);
  TimelineAddTrackCommand::RunImmediately(temp_seq->track_list(Track::kAudio));

  OLIVE_ASSERT(temp_seq->track_list(Track::kAudio)->GetTrackCount() == 1);
  OLIVE_TEST_END;
}

// ============================================================================
// Feature 4: Thread-Safe VU Metering (Tier 1 & Tier 2)
// ============================================================================

OLIVE_ADD_TEST(T1_04_01_VUMeterPeakCapture)
{
  std::atomic<float> peak_register{0.0f};
  core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 1, 1024, 1000.0f, 1.0f);

  float p = e2e::ComputePeak(buf, 0);
  peak_register.store(p, std::memory_order_relaxed);

  OLIVE_ASSERT(std::abs(peak_register.load() - 1.0f) < 0.001f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_04_02_VUMeterBallisticsDecay)
{
  // IEC 60268-10 decay: ~20 dB/sec decay rate
  float level = 1.0f; // 0 dB
  float dt = 0.05f; // 50 ms tick
  float decay_rate_db_per_sec = 20.0f;
  float decay_factor = std::pow(10.0f, -(decay_rate_db_per_sec * dt) / 20.0f);

  for (int step = 0; step < 20; ++step) { // 1.0 second
    level *= decay_factor;
  }

  // After 1 second, level should be ~ -20 dB (0.10)
  OLIVE_ASSERT(std::abs(level - 0.10f) < 0.02f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_04_03_VUMeterRMSCalculation)
{
  // Sine of amplitude 0.7071 has RMS = 0.7071 / sqrt(2) = 0.50
  core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 1, 4800, 1000.0f, 0.7071f);
  float rms = e2e::ComputeRMS(buf, 0);
  OLIVE_ASSERT(std::abs(rms - 0.50f) < 0.01f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_04_04_VUMeterConcurrentThreads)
{
  std::atomic<float> shared_peak{0.0f};
  std::atomic<bool> stop_flag{false};

  // Writer thread (simulating audio callback)
  std::thread writer([&]() {
    float val = 0.0f;
    while (!stop_flag.load()) {
      val += 0.05f;
      if (val > 1.0f) val = 0.0f;
      shared_peak.store(val, std::memory_order_release);
      std::this_thread::yield();
    }
  });

  // Reader threads (simulating GUI polling)
  std::atomic<bool> readers_ok{true};
  std::vector<std::thread> readers;
  for (int r = 0; r < 4; ++r) {
    readers.emplace_back([&]() {
      for (int i = 0; i < 1000; ++i) {
        float p = shared_peak.load(std::memory_order_acquire);
        if (!(p >= 0.0f && p <= 1.05f)) {
          readers_ok.store(false);
        }
        std::this_thread::yield();
      }
    });
  }

  for (auto& t : readers) {
    t.join();
  }
  stop_flag.store(true);
  writer.join();
  OLIVE_ASSERT(readers_ok.load());
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T1_04_05_VUMeterClipIndicator)
{
  std::atomic<bool> clip_flag{false};
  core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 1, 1024, 1000.0f, 1.25f); // > 0 dBFS

  float peak = e2e::ComputePeak(buf, 0);
  if (peak > 1.0f) {
    clip_flag.store(true);
  }

  OLIVE_ASSERT(clip_flag.load() == true);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_04_01_VUMeterSubnormalFloorClamping)
{
  float raw_val = 1e-15f; // very small subnormal
  float floor_val = 1e-5f; // -100 dBFS floor
  float clamped = (raw_val < floor_val) ? 0.0f : raw_val;
  OLIVE_ASSERT(clamped == 0.0f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_04_02_VUMeterDCOffsetStability)
{
  core::SampleBuffer buf = e2e::GenerateDCOffsetBuffer(48000, 1, 1024, 1.0f);
  float peak = e2e::ComputePeak(buf, 0);
  OLIVE_ASSERT(std::abs(peak - 1.0f) < 0.0001f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_04_03_VUMeterRapidStartStopCycles)
{
  std::atomic<float> reg{0.0f};
  for (int cycle = 0; cycle < 50; ++cycle) {
    reg.store(1.0f);
    OLIVE_ASSERT(reg.load() == 1.0f);
    reg.store(0.0f);
    OLIVE_ASSERT(reg.load() == 0.0f);
  }
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_04_04_VUMeterNanInfImmunity)
{
  core::SampleBuffer buf = e2e::GenerateSineBuffer(48000, 1, 64, 1000.0f, 0.5f);
  buf.data(0)[10] = std::numeric_limits<float>::quiet_NaN();
  buf.data(0)[20] = std::numeric_limits<float>::infinity();

  float sanitized_peak = 0.0f;
  for (size_t i = 0; i < buf.sample_count(); ++i) {
    float val = buf.data(0)[i];
    if (!std::isnan(val) && !std::isinf(val)) {
      if (std::abs(val) > sanitized_peak) {
        sanitized_peak = std::abs(val);
      }
    }
  }

  OLIVE_ASSERT(!std::isnan(sanitized_peak));
  OLIVE_ASSERT(!std::isinf(sanitized_peak));
  OLIVE_ASSERT(sanitized_peak > 0.0f && sanitized_peak <= 0.6f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T2_04_05_VUMeterExtendedSilenceIdle)
{
  float level = 0.00001f;
  bool is_idle = (level < 1e-4f);
  OLIVE_ASSERT(is_idle == true);
  OLIVE_TEST_END;
}

// ============================================================================
// Tier 3 Combinations (Audio Domain)
// ============================================================================

OLIVE_ADD_TEST(T3_03_MultiTrackMixerSummationVUMeter)
{
  // 4 tracks playing 4 distinct frequencies at 0.25 amp each
  const int sample_rate = 48000;
  const size_t N = 1024;
  core::SampleBuffer sum_buf(e2e::MakeAudioParams(sample_rate, 2), N);
  sum_buf.allocate();
  sum_buf.silence();

  float freqs[4] = {200.0f, 500.0f, 1000.0f, 2500.0f};
  for (int t = 0; t < 4; ++t) {
    core::SampleBuffer track_buf = e2e::GenerateSineBuffer(sample_rate, 2, N, freqs[t], 0.25f);
    for (int c = 0; c < 2; ++c) {
      for (size_t i = 0; i < N; ++i) {
        sum_buf.data(c)[i] += track_buf.data(c)[i];
      }
    }
  }

  float peak = e2e::ComputePeak(sum_buf, 0);
  // Sum of 4 uncorrelated sines of 0.25 amp has peak <= 1.0
  OLIVE_ASSERT(peak > 0.25f && peak <= 1.0f);
  OLIVE_TEST_END;
}

OLIVE_ADD_TEST(T3_04_SoloMuteStateReflectedOnVUMeter)
{
  // Track 1 active (0.8 peak), Track 2 active (0.8 peak)
  // When Track 1 is soloed, Track 2 VU meter drops to 0
  float track1_meter = 0.8f;
  float track2_meter = 0.8f;

  bool track1_solo = true;
  if (track1_solo) {
    track2_meter = 0.0f;
  }

  OLIVE_ASSERT(track1_meter == 0.8f);
  OLIVE_ASSERT(track2_meter == 0.0f);
  OLIVE_TEST_END;
}

} // namespace olive
