#ifndef OLIVE_BIQUAD_H
#define OLIVE_BIQUAD_H

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace olive {

enum BiquadFilterType {
  kFilterLowShelf = 0,
  kFilterHighShelf,
  kFilterPeaking,
  kFilterLowPass,
  kFilterHighPass,
  kFilterNotch,
  kFilterTypeCount
};

struct BiquadCoeffs {
  float b0 = 1.0f;
  float b1 = 0.0f;
  float b2 = 0.0f;
  float a1 = 0.0f;
  float a2 = 0.0f;

  static BiquadCoeffs Calculate(BiquadFilterType type,
                                double sample_rate,
                                double frequency,
                                double gain_db,
                                double q)
  {
    BiquadCoeffs c;

    if (sample_rate <= 0.0) {
      sample_rate = 48000.0;
    }

    // Clamp parameters within safe numerical bounds
    const double f_max = 0.49 * sample_rate;
    const double f0 = std::clamp(frequency, 10.0, f_max);
    const double Q = std::clamp(q, 0.1, 10.0);
    const double G = std::clamp(gain_db, -24.0, 24.0);

    // Bypass check: peaking or shelving with 0 dB gain is unity wire
    if ((type == kFilterPeaking || type == kFilterLowShelf || type == kFilterHighShelf) &&
        std::abs(G) < 1e-4) {
      c.b0 = 1.0f;
      c.b1 = 0.0f;
      c.b2 = 0.0f;
      c.a1 = 0.0f;
      c.a2 = 0.0f;
      return c;
    }

    constexpr double kPi = 3.14159265358979323846;
    const double omega = 2.0 * kPi * (f0 / sample_rate);
    const double cos_w = std::cos(omega);
    const double sin_w = std::sin(omega);
    const double alpha = sin_w / (2.0 * Q);
    const double A = std::pow(10.0, G / 40.0);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (type) {
    case kFilterPeaking: {
      b0 = 1.0 + alpha * A;
      b1 = -2.0 * cos_w;
      b2 = 1.0 - alpha * A;
      a0 = 1.0 + alpha / A;
      a1 = -2.0 * cos_w;
      a2 = 1.0 - alpha / A;
      break;
    }
    case kFilterLowShelf: {
      const double sqrt_A = std::sqrt(A);
      const double two_sqrt_A_alpha = 2.0 * sqrt_A * alpha;
      b0 = A * ((A + 1.0) - (A - 1.0) * cos_w + two_sqrt_A_alpha);
      b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cos_w);
      b2 = A * ((A + 1.0) - (A - 1.0) * cos_w - two_sqrt_A_alpha);
      a0 = (A + 1.0) + (A - 1.0) * cos_w + two_sqrt_A_alpha;
      a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cos_w);
      a2 = (A + 1.0) + (A - 1.0) * cos_w - two_sqrt_A_alpha;
      break;
    }
    case kFilterHighShelf: {
      const double sqrt_A = std::sqrt(A);
      const double two_sqrt_A_alpha = 2.0 * sqrt_A * alpha;
      b0 = A * ((A + 1.0) + (A - 1.0) * cos_w + two_sqrt_A_alpha);
      b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cos_w);
      b2 = A * ((A + 1.0) + (A - 1.0) * cos_w - two_sqrt_A_alpha);
      a0 = (A + 1.0) - (A - 1.0) * cos_w + two_sqrt_A_alpha;
      a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cos_w);
      a2 = (A + 1.0) - (A - 1.0) * cos_w - two_sqrt_A_alpha;
      break;
    }
    case kFilterLowPass: {
      b0 = (1.0 - cos_w) / 2.0;
      b1 = 1.0 - cos_w;
      b2 = (1.0 - cos_w) / 2.0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cos_w;
      a2 = 1.0 - alpha;
      break;
    }
    case kFilterHighPass: {
      b0 = (1.0 + cos_w) / 2.0;
      b1 = -(1.0 + cos_w);
      b2 = (1.0 + cos_w) / 2.0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cos_w;
      a2 = 1.0 - alpha;
      break;
    }
    case kFilterNotch: {
      b0 = 1.0;
      b1 = -2.0 * cos_w;
      b2 = 1.0;
      a0 = 1.0 + alpha;
      a1 = -2.0 * cos_w;
      a2 = 1.0 - alpha;
      break;
    }
    default:
      break;
    }

    // Normalize coefficients by a0
    const double inv_a0 = 1.0 / a0;
    c.b0 = static_cast<float>(b0 * inv_a0);
    c.b1 = static_cast<float>(b1 * inv_a0);
    c.b2 = static_cast<float>(b2 * inv_a0);
    c.a1 = static_cast<float>(a1 * inv_a0);
    c.a2 = static_cast<float>(a2 * inv_a0);

    return c;
  }
};

struct BiquadState {
  float s1 = 0.0f;
  float s2 = 0.0f;

  void Reset() {
    s1 = 0.0f;
    s2 = 0.0f;
  }
};

class Biquad {
public:
  using FilterType = BiquadFilterType;
  static constexpr BiquadFilterType kLowShelf = kFilterLowShelf;
  static constexpr BiquadFilterType kHighShelf = kFilterHighShelf;
  static constexpr BiquadFilterType kPeaking = kFilterPeaking;
  static constexpr BiquadFilterType kLowPass = kFilterLowPass;
  static constexpr BiquadFilterType kHighPass = kFilterHighPass;
  static constexpr BiquadFilterType kNotch = kFilterNotch;
  static constexpr BiquadFilterType kFilterTypeCount = olive::kFilterTypeCount;

  // Processes a single sample through Transposed Direct Form II
  static inline float ProcessSample(float x, const BiquadCoeffs &c, BiquadState &state)
  {
    const float y = c.b0 * x + state.s1;
    state.s1 = c.b1 * x - c.a1 * y + state.s2;
    state.s2 = c.b2 * x - c.a2 * y;
    return y;
  }

  // Processes an entire contiguous channel buffer in-place
  static void ProcessChannel(float *channel_data, size_t sample_count, const BiquadCoeffs &c)
  {
    float s1 = 0.0f;
    float s2 = 0.0f;

    const float b0 = c.b0;
    const float b1 = c.b1;
    const float b2 = c.b2;
    const float a1 = c.a1;
    const float a2 = c.a2;

    for (size_t i = 0; i < sample_count; ++i) {
      const float x = channel_data[i];
      const float y = b0 * x + s1;
      s1 = b1 * x - a1 * y + s2;
      s2 = b2 * x - a2 * y;
      channel_data[i] = y;
    }
  }
};

} // namespace olive

#endif // OLIVE_BIQUAD_H
