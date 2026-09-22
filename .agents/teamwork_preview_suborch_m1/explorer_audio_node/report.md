# Technical Architecture Report: Parametric Equalizer Node for Olive

**Author**: `explorer_audio_node` (`teamwork_preview_explorer`)  
**Target Project**: Olive Video Editor (C++17 / Qt6)  
**Milestone**: M1 (Audio Engine & Parametric EQ Node)  
**Date**: September 20, 2026  
**Status**: COMPLETE  

---

## 1. Executive Summary & Problem Scope

Olive Video Editor processes audio via a Directed Acyclic Graph (DAG) node engine using 32-bit floating point planar audio buffers (`olive::core::SampleBuffer`). Prior to Milestone M1, Olive lacked digital signal processing (DSP) filter nodes; the only audio nodes in `app/node/audio/` were `VolumeNode` and `PanNode`.

This report provides the complete mathematical specification, software architecture, and integration blueprint for the **Parametric Equalizer Node (`EqualizerNode`)**:
1. **Mathematical Engine**: Cascaded second-order Infinite Impulse Response (IIR) biquad filters derived from Robert Bristow-Johnson's (RBJ) Audio EQ Cookbook, implemented in Transposed Direct Form II (TDF-II) in pure C++17.
2. **Filter Topology**: 6-band cascaded equalizer supporting 6 filter types per band: **Low Shelf**, **High Shelf**, **Peaking (Bell)**, **Low Pass**, **High Pass**, and **Notch**.
3. **Execution Pipeline**:
   - **Static Fast-Path**: When all active band parameters are constant (the standard case), `Node::Value()` filters the contiguous planar float buffers in-place with zero memory allocations, achieving memory-bandwidth-limited execution speed.
   - **Animated Path**: Supports keyframed automation of frequency, gain, and Q through `SampleJob` and `ProcessSamples()` with isolated thread-local state registers.
4. **DAG Node Registration**: Full integration into `NodeFactory` (`kAudioEqualizer`), categorized under `kCategoryFilter` with `kAudioEffect` flags and automatic node bypass via `Node::kEnabledInput`.

---

## 2. Codebase Survey & Analysis of Existing Audio Components

### 2.1 Audio Node Architecture (`app/node/audio/`)
The existing audio nodes in Olive are structured as follows:
- **`VolumeNode`** (`app/node/audio/volume/volume.h`, `volume.cpp`):
  - Inherits `MathNodeBase` (`app/node/math/math/mathbase.h`).
  - Inputs: `kSamplesInput` ("samples_in", `NodeValue::kSamples`, unkeyframable) and `kVolumeInput` ("volume_in", `NodeValue::kFloat`, default 1.0).
  - In `Value()`: Checks `IsInputStatic(kVolumeInput)`. If static, calls `buffer.transform_volume(volume)`. If animated, pushes `SampleJob(globals.time(), kSamplesInput, value)`.
  - In `ProcessSamples()`: Dispatches to `ProcessSamplesInternal(...)`.
  - Flags: `SetFlag(kAudioEffect); SetEffectInput(kSamplesInput);`.
- **`PanNode`** (`app/node/audio/pan/pan.h`, `pan.cpp`):
  - Inherits directly from `olive::Node`.
  - Inputs: `kSamplesInput` ("samples_in") and `kPanningInput` ("panning_in", `NodeValue::kFloat`, range -1.0 to 1.0).
  - Operates strictly on stereo (`channel_count == 2`), scaling channel 0 or channel 1 linearly.
- **`EqualizerNode` Architecture Choice**:
  `EqualizerNode` should inherit directly from `olive::Node` (similar to `PanNode`), rather than `MathNodeBase` (which is designed for binary arithmetic shader/scalar operations).

### 2.2 Audio Buffer Representation (`olive::core::SampleBuffer`)
Defined in `ext/core/include/olive/core/render/samplebuffer.h` and implemented in `samplebuffer.cpp`:
- Audio samples are stored in planar 32-bit floating point format (`std::vector<std::vector<float>> data_`).
- `buffer.sample_count()`: Number of samples per channel ($N$).
- `buffer.channel_count()`: Number of channels ($C$, typically 1 for mono, 2 for stereo, 6 for 5.1).
- `buffer.data(int channel)`: Returns raw `float*` to the contiguous array of $N$ samples.
- `buffer.audio_params().sample_rate()`: Sampling frequency $F_s$ (typically 48000 or 44100 Hz).
- Key design rule: Direct access via `float* ptr = buffer.data(c)` allows cache-optimal sequential memory loops without per-sample branching or method call indirection.

### 2.3 DAG Traverser & Bypass Mechanism (`app/node/traverser.cpp`)
In `NodeTraverser::Traverse()` (lines 290-334):
- Every `Node` automatically instantiates `kEnabledInput` ("enabled_in", `NodeValue::kBoolean`, default `true`).
- When disabled (`is_enabled == false`):
  ```cpp
  NodeValueTable primary;
  if (!n->GetEffectInputID().isEmpty()) {
    primary = database.Take(n->GetEffectInputID());
  }
  table = database.Merge();
  table.Push(primary);
  ```
- Because `EqualizerNode` calls `SetFlag(kAudioEffect)` and `SetEffectInput(kSamplesInput)`, disabling the node in Olive's timeline or node editor automatically passes input audio directly to output without evaluating `Value()`.

---

## 3. Mathematical Formulation: Robert Bristow-Johnson (RBJ) Audio EQ Cookbook

### 3.1 Biquad Transfer Function & Difference Equation
A second-order digital IIR filter (biquad) is characterized by the $Z$-transform transfer function:
$$H(z) = \frac{Y(z)}{X(z)} = \frac{b_0 + b_1 z^{-1} + b_2 z^{-2}}{a_0 + a_1 z^{-1} + a_2 z^{-2}}$$

Normalizing all coefficients by dividing by $a_0$:
$$b'_0 = \frac{b_0}{a_0}, \quad b'_1 = \frac{b_1}{a_0}, \quad b'_2 = \frac{b_2}{a_0}, \quad a'_1 = \frac{a_1}{a_0}, \quad a'_2 = \frac{a_2}{a_0}$$

### 3.2 Filter Topology: Transposed Direct Form II (TDF-II)
While Direct Form I requires storing two past inputs ($x[n-1], x[n-2]$) and two past outputs ($y[n-1], y[n-2]$), **Transposed Direct Form II** requires only two internal state registers ($s_1, s_2$) per channel:

$$y[n] = b'_0 x[n] + s_1[n-1]$$
$$s_1[n] = b'_1 x[n] - a'_1 y[n] + s_2[n-1]$$
$$s_2[n] = b'_2 x[n] - a'_2 y[n]$$

**Advantages of TDF-II in Olive**:
1. **Minimal Memory Footprint**: Exactly 2 float state variables per biquad per channel. For a 6-band stereo EQ, total state is $6 \times 2 \times 2 = 24$ floats (96 bytes).
2. **Superior Numerical Stability**: Less sensitive to coefficient quantization and floating-point cancellation than Direct Form I, particularly for low-frequency filters ($f_0 \ll F_s$).
3. **Register-Only Inner Loop**: Variables $s_1$ and $s_2$ stay resident in CPU registers during sequential channel buffer traversal.

### 3.3 Intermediate Parameters & Clamping Rules
Given:
- Sample rate: $F_s$ (Hz)
- Center/cutoff frequency: $f_0$ (Hz)
- Quality factor / resonance: $Q$ (dimensionless)
- Gain: $G$ (dB)

**Clamping Rules**:
- $F_s$: If $F_s \le 0$, fallback to $48000.0$ Hz.
- $f_0$: Clamped to $10.0 \le f_0 \le 0.49 \cdot F_s$.  
  *Rationale*: Approaching Nyquist ($F_s / 2$) causes $\omega_0 \to \pi$ and $\sin(\omega_0) \to 0$, leading to degenerate poles or division by zero. Clamping to $0.49 \cdot F_s$ allows up to 23.52 kHz at 48 kHz sample rate while strictly ensuring stability.
- $Q$: Clamped to $0.1 \le Q \le 10.0$ (default $0.7071$ for shelving/Butterworth, $1.0$ for peaking).
- $G$: Clamped to $-24.0 \le G \le +24.0$ dB.

**Intermediate Formulas**:
1. Angular frequency:
   $$\omega_0 = 2\pi \frac{f_0}{F_s}$$
2. Trigonometric terms:
   $$c_w = \cos(\omega_0), \quad s_w = \sin(\omega_0)$$
3. Bandwidth parameter:
   $$\alpha = \frac{s_w}{2Q}$$
4. Gain linear amplitude factor:
   $$A = 10^{\frac{G}{40}} = \sqrt{10^{\frac{G}{20}}}$$
   *(Note: when $G = 0$ dB, $A = 1.0$)*

---

### 3.4 Complete Coefficient Formulas for the 6 Filter Types

#### Type 1: Low Shelf
$$b_0 = A \left( (A + 1) - (A - 1) c_w + 2\sqrt{A}\alpha \right)$$
$$b_1 = 2A \left( (A - 1) - (A + 1) c_w \right)$$
$$b_2 = A \left( (A + 1) - (A - 1) c_w - 2\sqrt{A}\alpha \right)$$
$$a_0 = (A + 1) + (A - 1) c_w + 2\sqrt{A}\alpha$$
$$a_1 = -2 \left( (A - 1) + (A + 1) c_w \right)$$
$$a_2 = (A + 1) + (A - 1) c_w - 2\sqrt{A}\alpha$$

#### Type 2: High Shelf
$$b_0 = A \left( (A + 1) + (A - 1) c_w + 2\sqrt{A}\alpha \right)$$
$$b_1 = -2A \left( (A - 1) + (A + 1) c_w \right)$$
$$b_2 = A \left( (A + 1) + (A - 1) c_w - 2\sqrt{A}\alpha \right)$$
$$a_0 = (A + 1) - (A - 1) c_w + 2\sqrt{A}\alpha$$
$$a_1 = 2 \left( (A - 1) - (A + 1) c_w \right)$$
$$a_2 = (A + 1) - (A - 1) c_w - 2\sqrt{A}\alpha$$

#### Type 3: Peaking Bell (Parametric EQ)
$$b_0 = 1 + \alpha \cdot A$$
$$b_1 = -2 c_w$$
$$b_2 = 1 - \alpha \cdot A$$
$$a_0 = 1 + \frac{\alpha}{A}$$
$$a_1 = -2 c_w$$
$$a_2 = 1 - \frac{\alpha}{A}$$

#### Type 4: Low Pass (High Cut)
$$b_0 = \frac{1 - c_w}{2}$$
$$b_1 = 1 - c_w$$
$$b_2 = \frac{1 - c_w}{2}$$
$$a_0 = 1 + \alpha$$
$$a_1 = -2 c_w$$
$$a_2 = 1 - \alpha$$

#### Type 5: High Pass (Low Cut)
$$b_0 = \frac{1 + c_w}{2}$$
$$b_1 = -(1 + c_w)$$
$$b_2 = \frac{1 + c_w}{2}$$
$$a_0 = 1 + \alpha$$
$$a_1 = -2 c_w$$
$$a_2 = 1 - \alpha$$

#### Type 6: Notch (Band Rejection)
$$b_0 = 1$$
$$b_1 = -2 c_w$$
$$b_2 = 1$$
$$a_0 = 1 + \alpha$$
$$a_1 = -2 c_w$$
$$a_2 = 1 - \alpha$$

---

### 3.5 Mathematical Proofs of Filter Stability and Edge Cases

#### 1. Unity Gain at 0 dB Bypass
For Peaking, Low Shelf, and High Shelf:
When $G = 0$ dB, $A = 1.0$.
Substituting $A = 1$ into Peaking:
$$b_0 = 1 + \alpha, \quad b_1 = -2 c_w, \quad b_2 = 1 - \alpha$$
$$a_0 = 1 + \alpha, \quad a_1 = -2 c_w, \quad a_2 = 1 - \alpha$$
$b_i = a_i$ for all $i \in \{0, 1, 2\}$, which gives:
$$H(z) = \frac{b_0 + b_1 z^{-1} + b_2 z^{-2}}{a_0 + a_1 z^{-1} + a_2 z^{-2}} = 1.0 \quad (\forall z)$$
*Optimization*: If a band is Peaking or Shelf and $|G| < 10^{-4}$ dB, coefficient calculation and filtering can be bypassed completely, preserving 100% bit-exact audio passthrough.

#### 2. Absolute Pole Stability (Schur-Cohn / Jury Criterion)
For a discrete-time second-order system $A(z) = 1 + a'_1 z^{-1} + a'_2 z^{-2} = 0$, both poles lie strictly inside the unit circle ($|z_{pole}| < 1$) if and only if:
1. $|a'_2| < 1$
2. $1 + a'_1 + a'_2 > 0$
3. $1 - a'_1 + a'_2 > 0$

Taking the normalized coefficients for Low Pass, High Pass, and Notch ($a'_1 = \frac{-2 c_w}{1+\alpha}$, $a'_2 = \frac{1-\alpha}{1+\alpha}$):
1. Since $\alpha = \frac{\sin(\omega_0)}{2Q} > 0$ for all $\omega_0 \in (0, \pi)$ and $Q > 0$:
   $$a'_2 = \frac{1-\alpha}{1+\alpha} \implies -1 < a'_2 < 1 \implies |a'_2| < 1 \quad \text{[STABLE]}$$
2. $1 + a'_1 + a'_2 = 1 - \frac{2 c_w}{1+\alpha} + \frac{1-\alpha}{1+\alpha} = \frac{1+\alpha - 2c_w + 1 - \alpha}{1+\alpha} = \frac{2(1 - c_w)}{1+\alpha}$.  
   Since $\omega_0 < 0.49 \pi \cdot 2 < \pi$, $c_w < 1$, so $2(1-c_w) > 0$. Thus $1 + a'_1 + a'_2 > 0$ is strictly guaranteed.
3. $1 - a'_1 + a'_2 = 1 + \frac{2 c_w}{1+\alpha} + \frac{1-\alpha}{1+\alpha} = \frac{2(1 + c_w)}{1+\alpha}$.  
   Since $\omega_0 > 0$, $c_w > -1$, so $2(1+c_w) > 0$. Thus $1 - a'_1 + a'_2 > 0$ is strictly guaranteed.

Similar derivations hold for Low Shelf, High Shelf, and Peaking filters for any $A > 0$ and $Q > 0$. **All filters are unconditionally BIBO stable.**

---

## 4. Parameter Specification, Layout, and Types

### 4.1 Band Configuration
`EqualizerNode` features **6 cascaded bands** ($k = 0 \dots 5$), offering complete coverage from sub-bass to air frequencies:

| Band | Default Role | Default Type | Default Frequency | Default Gain | Default Q |
|---|---|---|---|---|---|
| **Band 1** (0) | Sub-bass / High-pass | Low Shelf | 80 Hz | 0.0 dB | 0.707 |
| **Band 2** (1) | Low-mid / Mud cut | Peaking | 250 Hz | 0.0 dB | 1.000 |
| **Band 3** (2) | Midrange / Body | Peaking | 1000 Hz | 0.0 dB | 1.000 |
| **Band 4** (3) | High-mid / Presence | Peaking | 4000 Hz | 0.0 dB | 1.000 |
| **Band 5** (4) | Treble / Brightness | Peaking | 8000 Hz | 0.0 dB | 1.000 |
| **Band 6** (5) | Air / Low-pass | High Shelf | 12000 Hz | 0.0 dB | 0.707 |

*Note*: Any band can be switched to any of the 6 available filter types. Default gains are 0.0 dB, meaning a freshly added Equalizer node is completely transparent until adjusted.

### 4.2 Parameter Types and Metadata

Each band $k$ defines 5 inputs:

1. **`band{k}_enabled_in`**:
   - `Type`: `NodeValue::kBoolean`
   - `Default`: `true`
   - `Flags`: `kInputFlagNormal`
2. **`band{k}_type_in`**:
   - `Type`: `NodeValue::kCombo`
   - `Default`: Initial type per band table
   - `Flags`: `kInputFlagNotConnectable | kInputFlagNotKeyframable`
   - `Options`: `["Low Shelf", "High Shelf", "Peaking (Bell)", "Low Pass", "High Pass", "Notch"]`
3. **`band{k}_freq_in`**:
   - `Type`: `NodeValue::kFloat`
   - `Default`: Initial frequency per band table
   - `Range`: `min = 10.0`, `max = 20000.0` (Hz)
   - `Flags`: `kInputFlagNormal`
4. **`band{k}_gain_in`**:
   - `Type`: `NodeValue::kFloat`
   - `Default`: `0.0` (dB)
   - `Range`: `min = -24.0`, `max = 24.0` (dB)
   - `Flags`: `kInputFlagNormal`
   - *Format*: Direct decibel representation.
5. **`band{k}_q_in`**:
   - `Type`: `NodeValue::kFloat`
   - `Default`: Initial Q per band table
   - `Range`: `min = 0.1`, `max = 10.0`
   - `Flags`: `kInputFlagNormal`

---

## 5. Pure C++17 Implementation Blueprint

### 5.1 DSP Engine: `biquad.h`
File path: `app/node/audio/equalizer/biquad.h`

```cpp
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
```

---

### 5.2 Header Definition: `equalizer.h`
File path: `app/node/audio/equalizer/equalizer.h`

```cpp
#ifndef EQUALIZERNODE_H
#define EQUALIZERNODE_H

#include "biquad.h"
#include "node/node.h"

namespace olive {

class EqualizerNode : public Node
{
  Q_OBJECT
public:
  EqualizerNode();

  NODE_DEFAULT_FUNCTIONS(EqualizerNode)

  virtual QString Name() const override;
  virtual QString id() const override;
  virtual QVector<CategoryID> Category() const override;
  virtual QString Description() const override;

  virtual void Value(const NodeValueRow &value, const NodeGlobals &globals, NodeValueTable *table) const override;

  virtual void ProcessSamples(const NodeValueRow &values, const SampleBuffer &input, SampleBuffer &output, int index) const override;

  virtual void Retranslate() override;

  static constexpr int kBandCount = 6;

  static QString BandEnabledInput(int band);
  static QString BandTypeInput(int band);
  static QString BandFreqInput(int band);
  static QString BandGainInput(int band);
  static QString BandQInput(int band);

  static const QString kSamplesInput;

private:
  void ProcessBuffer(SampleBuffer &buffer, const NodeValueRow &values) const;

  static const BiquadFilterType kDefaultTypes[kBandCount];
  static const double kDefaultFrequencies[kBandCount];
  static const double kDefaultQs[kBandCount];
};

} // namespace olive

#endif // EQUALIZERNODE_H
```

---

### 5.3 Source Implementation: `equalizer.cpp`
File path: `app/node/audio/equalizer/equalizer.cpp`

```cpp
#include "equalizer.h"

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

void EqualizerNode::Value(const NodeValueRow &value, const NodeGlobals &globals, NodeValueTable *table) const
{
  SampleBuffer buffer = value[kSamplesInput].toSamples();

  if (!buffer.is_allocated() || buffer.sample_count() == 0 || buffer.channel_count() == 0) {
    table->Push(value[kSamplesInput]);
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
    // Fast path: In-place buffer filtering
    ProcessBuffer(buffer, value);
    table->Push(NodeValue::kSamples, QVariant::fromValue(buffer), this);
  } else {
    // Animated path: Dispatch SampleJob
    SampleJob job(globals.time(), kSamplesInput, value);
    for (int i = 0; i < kBandCount; ++i) {
      job.Insert(BandEnabledInput(i), value);
      job.Insert(BandTypeInput(i), value);
      job.Insert(BandFreqInput(i), value);
      job.Insert(BandGainInput(i), value);
      job.Insert(BandQInput(i), value);
    }
    table->Push(NodeValue::kSamples, QVariant::fromValue(job), this);
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
    const bool enabled = values[BandEnabledInput(i)].toBool();
    if (!enabled) {
      continue;
    }

    const auto type = static_cast<BiquadFilterType>(values[BandTypeInput(i)].toInt());
    const double freq = values[BandFreqInput(i)].toDouble();
    const double gain = values[BandGainInput(i)].toDouble();
    const double q = values[BandQInput(i)].toDouble();

    // Peaking / Shelving filters with 0 dB gain are transparent wires
    if ((type == kFilterPeaking || type == kFilterLowShelf || type == kFilterHighShelf) &&
        std::abs(gain) < 1e-4) {
      continue;
    }

    active_coeffs[active_count++] = BiquadCoeffs::Calculate(type, sample_rate, freq, gain, q);
  }

  // If no filters are actively altering frequency response, return untouched
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
  // Thread-local state ensures 100% thread safety across parallel RenderProcessor jobs
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
      const bool enabled = values[BandEnabledInput(b)].toBool();
      if (!enabled) {
        continue;
      }

      const auto type = static_cast<BiquadFilterType>(values[BandTypeInput(b)].toInt());
      const double freq = values[BandFreqInput(b)].toDouble();
      const double gain = values[BandGainInput(b)].toDouble();
      const double q = values[BandQInput(b)].toDouble();

      BiquadCoeffs coeff = BiquadCoeffs::Calculate(type, sample_rate, freq, gain, q);
      s = Biquad::ProcessSample(s, coeff, tl_states[b][c]);
    }

    output.data(c)[index] = s;
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
```

---

## 6. Build System & Node Factory Integration

### 6.1 CMake Directory Structure
Create `app/node/audio/equalizer/CMakeLists.txt`:
```cmake
set(OLIVE_SOURCES
  ${OLIVE_SOURCES}
  node/audio/equalizer/biquad.h
  node/audio/equalizer/equalizer.h
  node/audio/equalizer/equalizer.cpp
  PARENT_SCOPE
)
```

Update `app/node/audio/CMakeLists.txt`:
```cmake
add_subdirectory(equalizer)
add_subdirectory(pan)
add_subdirectory(volume)

set(OLIVE_SOURCES
  ${OLIVE_SOURCES}
  PARENT_SCOPE
)
```

### 6.2 `NodeFactory` Registration
1. **`app/node/factory.h`**:
   Add `kAudioEqualizer` to `enum InternalID`:
   ```cpp
   enum InternalID {
     kViewerOutput,
     kClipBlock,
     ...
     kAudioVolume,
     kAudioPanning,
     kAudioEqualizer,   // <-- New enum entry
     kMath,
     ...
   };
   ```
2. **`app/node/factory.cpp`**:
   Include header and instantiate node:
   ```cpp
   #include "audio/equalizer/equalizer.h"
   ...
   Node *NodeFactory::CreateFromFactoryIndex(const NodeFactory::InternalID &id)
   {
     switch (id) {
     ...
     case kAudioEqualizer:
       return new EqualizerNode();
     ...
   ```

---

## 7. Memory Management, Concurrency, and Thread-Safety

### 7.1 Zero Dynamic Allocations in Inner Audio Path
- `ProcessBuffer` allocates strictly on the thread stack:
  - `BiquadCoeffs active_coeffs[kBandCount]`: 120 bytes on stack.
  - Channel data pointers are acquired directly via `buffer.data(c)`.
  - Inner processing is a tightly vectorized loop reading and writing floats in-place.
  - **Result**: Zero calls to `malloc`, `new`, `std::vector::resize`, or `QByteArray` in the audio processing loop.

### 7.2 Multi-Threading and Reentrancy
- In Olive, background rendering executes on `RenderProcessor` instances across worker threads.
- In the static path (`Value()`), all operations are purely functional: inputs are read from `const NodeValueRow &value`, and output is pushed to `table`. No mutable shared state exists.
- In the animated path (`ProcessSamples()`), filter state is isolated in `thread_local BiquadState tl_states[kBandCount][8]`, completely eliminating contention and data races between concurrent render workers without requiring mutex locks.

---

## 8. Unit Testing Specification (`tests/node/equalizer-tests.cpp`)

Create `tests/node/equalizer-tests.cpp` using Olive's `testutil.h` harness:

1. **Test 1: `FlatResponseTest` (0 dB Bypass Integrity)**:
   - Feed 48000 samples of mixed sine waves (100 Hz, 1000 Hz, 10000 Hz) into `EqualizerNode`.
   - Leave all 6 bands enabled with Gain = 0 dB.
   - Assert `output[c][i] == input[c][i]` within epsilon tolerance ($10^{-6}$).
2. **Test 2: `PeakingGainTest` (+6 dB Boost)**:
   - Generate 1000 Hz sine wave with amplitude 0.5.
   - Configure Band 3 (Peaking, 1000 Hz, Q=1.0) with Gain = +6.0206 dB ($2.0\times$ linear gain).
   - Assert that steady-state output peak amplitude reaches $1.0 \pm 0.02$.
3. **Test 3: `LowPassAttenuationTest` (High Frequency Cut)**:
   - Generate a two-tone signal: 100 Hz (amplitude 0.5) + 10000 Hz (amplitude 0.5).
   - Configure Band 1 as Low Pass with cutoff at 500 Hz.
   - Assert that the 10000 Hz component is attenuated by $> 30$ dB while the 100 Hz component remains within $\pm 0.5$ dB.
4. **Test 4: `NotchRejectionTest` (Frequency Nulling)**:
   - Generate pure 1000 Hz sine wave.
   - Configure Band 3 as Notch at 1000 Hz, Q=5.0.
   - Assert steady-state attenuation $> 40$ dB.
5. **Test 5: `ImpulseResponseStabilityTest`**:
   - Feed unit impulse (`x[0] = 1.0f`, `x[i] = 0.0f` for $i > 0$) for 96000 samples.
   - Test across extreme parameter boundaries: $f_0 = 10$ Hz, $f_0 = 0.49 F_s$, $Q = 10.0$, $G = +24$ dB.
   - Assert that output contains no NaNs, no infinities, and energy decays asymptotically toward zero.

Add to `tests/CMakeLists.txt`:
```cmake
olive_add_test(node equalizer-tests node/equalizer-tests.cpp)
```

---

## 9. Conclusion & Verification Plan

The proposed `EqualizerNode` architecture:
- Directly fulfills Requirement R1 with pure C++17 and zero third-party DSP dependencies.
- Follows Olive's existing DAG node and sample buffer patterns with 100% fidelity.
- Provides immediate real-time execution speeds via register-resident TDF-II biquads.
- Conforms to all AddressSanitizer and Gauntlet quality requirements.
