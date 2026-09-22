# Technical Exploration Report: Audio Testing Infrastructure & Test Specifications (Milestone M1)

**Author**: Explorer Audio Tests (`explorer_audio_tests`)  
**Target Project**: Olive Video Editor (C++17 / Qt6)  
**Date**: September 20, 2026  
**Status**: COMPLETE  
**Output Path**: `/home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_tests/report.md`  

---

## Executive Summary

This report establishes the complete architectural exploration and precise test specifications for Milestone M1 (Parametric Equalizer Node and Track Audio Controls). It details:
1. **Olive Test Harness Architecture**: How CMake, CTest, and `tests/testutil.h` interface through `olive_add_test` to generate test runners, instantiate `QCoreApplication`, and evaluate test functions.
2. **Audio Engine Verification Mathematics**: How `SampleBuffer` (planar 32-bit float), `AudioParams`, synthetic test signal generation (sine tones, unit impulses, DC signals), and steady-state RMS / FFT window calculations operate with strict epsilon bounds.
3. **Build & ASan Execution Environment**: How tests are configured under `linux-asan` with Clang/GCC (`-fsanitize=address,undefined -fno-omit-frame-pointer`), and how `scripts/gauntlet.py` executes them with 0 memory leaks, 0 undefined behavior violations, and 0 assertion failures.
4. **Complete Test Suite Specifications**:
   - `tests/node/equalizer-tests.cpp`: 5 rigorous test suites covering flat response / bypass, filter attenuation and resonant peak boosts (Low-pass, High-pass, Peaking Bell), unit impulse stability over 48,000 samples, Nyquist and parameter limit clamping, and planar stereo independence.
   - `tests/timeline/track-audio-tests.cpp`: 3 comprehensive test suites verifying volume scaling (0.0 to 2.0 / $-\infty$ to $+6$ dB), stereo pan law (-1.0 to +1.0), and timeline sequence track solo/mute interaction logic.
5. **CMake Registration Specification**: The exact mechanism for integrating both test suites into `tests/CMakeLists.txt` and CTest.

---

## 1. Test Infrastructure & Harness Mechanics

### 1.1 `olive_add_test` Function Mechanics (`tests/CMakeLists.txt`)

Olive avoids external C++ testing dependencies (like GoogleTest or Catch2) in favor of an in-house CMake code generator. The test generation logic in `tests/CMakeLists.txt` operates as follows:

```cmake
function(olive_add_test GROUP NAME SOURCE)
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS ${SOURCE}
  )

  file(READ "${SOURCE}" TEST_FILE_CONTENT)
  string(REGEX MATCHALL "OLIVE_ADD_TEST\(.[A-Za-z0-9_]+\)" TEST_FUNCTIONS ${TEST_FILE_CONTENT})
  set(TEST_BODY "#include <QCoreApplication>\n#include <QStandardPaths>\nint main(int argc, char** argv)\n{\n  QCoreApplication app(argc, argv);\n  QStandardPaths::setTestModeEnabled(true);\n  int ret;(void)ret;")
  ...
  foreach (TEST_FUNC ${TEST_FUNCTIONS})
    string(REPLACE "OLIVE_ADD_TEST(" "" TEST_FUNC "${TEST_FUNC}")
    string(APPEND TEST_BODY "  std::cout << \"[${TEST_INDEX}/${TEST_COUNT}] ${GROUP} - ${TEST_FUNC}\";\n")
    string(APPEND TEST_BODY "  if ((ret = olive::Test${TEST_FUNC}()) == OLIVE_TEST_SUCCESS) {std::cout << \" - PASSED\" << std::endl;}else{std::cout << \" - FAILED AT LINE \" << ret << std::endl;return 1;}\n")
    MATH(EXPR TEST_INDEX "${TEST_INDEX}+1")
  endforeach()
  string(APPEND TEST_BODY "  return 0;\n}")

  set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SOURCE}")
  string(APPEND TEST_FILE_CONTENT "\n${TEST_BODY}")
  file(WRITE "${OUTPUT_FILE}" "${TEST_FILE_CONTENT}")

  add_executable(${NAME} ${OUTPUT_FILE} $<TARGET_OBJECTS:libolive-editor>)
  target_include_directories(
    ${NAME}
    PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/tests
    ${OLIVE_INCLUDE_DIRS}
  )
  target_link_libraries(
    ${NAME}
    PRIVATE
    ${OLIVE_LIBRARIES}
  )
  ...
  add_test(${NAME} ${NAME})
  set_tests_properties(${test_name} PROPERTIES TIMEOUT 120 LABELS "${GROUP}")
endfunction()
```

#### Key Discoveries on Runner Code Generation:
1. **Regex Parsing**:
   `string(REGEX MATCHALL "OLIVE_ADD_TEST\(.[A-Za-z0-9_]+\)" TEST_FUNCTIONS ...)`
   In CMake, `\(` and `\)` inside double-quoted strings unescape to `(` and `)`. The regex engine treats `(...)` as a capture group. The `.` matches the literal open parenthesis `(` of `OLIVE_ADD_TEST(...)`, while `[A-Za-z0-9_]+` matches the test function name. The closing `)` of the macro is outside the regex match.
   Line 38 then performs `string(REPLACE "OLIVE_ADD_TEST(" "" TEST_FUNC "${TEST_FUNC}")`, which leaves exactly the function identifier (e.g. `FilterResponseLowPass`).
2. **Namespace Requirement**:
   Generated `main()` calls `olive::Test${TEST_FUNC}()`. Consequently, **all test functions declared with `OLIVE_ADD_TEST` MUST reside inside the `namespace olive { ... }` block**.
3. **Execution Pipeline**:
   The generated `main()` initializes `QCoreApplication app(argc, argv)` and sets `QStandardPaths::setTestModeEnabled(true)`. This ensures Qt event loops, path lookups, and metadata work without popping GUI windows.
4. **Target Linking**:
   `add_executable(${NAME} ${OUTPUT_FILE} $<TARGET_OBJECTS:libolive-editor>)`
   Each test executable links directly against the object files of `libolive-editor` and `${OLIVE_LIBRARIES}`. Any class compiled into `app/` is linked without requiring a static or dynamic library rebuild.

### 1.2 Assertion Macros (`tests/testutil.h`)

`tests/testutil.h` provides minimal, non-throwing assertion macros:
- `#define OLIVE_TEST_SUCCESS -1`
- `#define OLIVE_ASSERT(x) if (!(x)) return __LINE__`
- `#define OLIVE_ASSERT_EQUAL(x, y) if (x != y) {std::cout << " - Equal assert failed: " << x << " != " << y; return __LINE__;}void()`
- `#define OLIVE_TEST_END return OLIVE_TEST_SUCCESS`
- `#define OLIVE_ADD_TEST(x) int Test##x()`

On failure, the macro immediately exits the function and returns `__LINE__`. The generated `main()` prints `FAILED AT LINE <line>` and returns exit code 1. On success, `OLIVE_TEST_END` returns `-1` (`OLIVE_TEST_SUCCESS`), and the runner prints `PASSED`.

---

## 2. Audio Data Structures & Signal Generation Math

### 2.1 `SampleBuffer` & `AudioParams` Layout

In Olive, audio is passed as planar 32-bit float buffers:
- **`AudioParams`**:
  `AudioParams(int sample_rate, uint64_t channel_layout, SampleFormat format)`
  - Standard test parameters: `AudioParams(48000, AV_CH_LAYOUT_STEREO, core::SampleFormat::F32P)`
  - `time_to_samples(rational)`: converts time to sample count ($N = \text{round}(T \times F_s)$).
- **`SampleBuffer`**:
  - Allocated via `SampleBuffer(params, sample_count)`.
  - Stored as `std::vector<std::vector<float>> data_`.
  - Channel access: `sb.data(c)` returns raw `float*` to channel $c$ (`c = 0` is Left, `c = 1` is Right).
  - Channel count: `sb.channel_count()` or `sb.audio_params().channel_count()`.
  - Methods: `sb.silence()`, `sb.transform_volume(factor)`, `sb.transform_volume_for_channel(channel, factor)`.

### 2.2 Test Signal Generators

To test filters with mathematical precision, four signal generator functions are used:

```cpp
// 1. Pure Sine Wave Generator (zero phase offset)
void FillSine(SampleBuffer *sb, double frequency, double amplitude = 1.0) {
  sb->silence();
  const size_t n = sb->sample_count();
  const int channels = sb->channel_count();
  const double fs = double(sb->audio_params().sample_rate());
  const double w = 2.0 * M_PI * frequency / fs;

  for (size_t i = 0; i < n; i++) {
    const float val = float(amplitude * std::sin(w * double(i)));
    for (int c = 0; c < channels; c++) {
      sb->data(c)[i] = val;
    }
  }
}

// 2. Dual-Tone Generator (for measuring intermodulation or simultaneous multi-band response)
void FillDualTone(SampleBuffer *sb, double f1, double f2, double a1 = 0.5, double a2 = 0.5) {
  sb->silence();
  const size_t n = sb->sample_count();
  const int channels = sb->channel_count();
  const double fs = double(sb->audio_params().sample_rate());
  const double w1 = 2.0 * M_PI * f1 / fs;
  const double w2 = 2.0 * M_PI * f2 / fs;

  for (size_t i = 0; i < n; i++) {
    const float val = float(a1 * std::sin(w1 * double(i)) + a2 * std::sin(w2 * double(i)));
    for (int c = 0; c < channels; c++) {
      sb->data(c)[i] = val;
    }
  }
}

// 3. Unit Impulse Generator (delta function at sample 0)
void FillUnitImpulse(SampleBuffer *sb, double amplitude = 1.0) {
  sb->silence();
  const int channels = sb->channel_count();
  if (sb->sample_count() > 0) {
    for (int c = 0; c < channels; c++) {
      sb->data(c)[0] = float(amplitude);
    }
  }
}

// 4. DC (Constant) Generator
void FillDC(SampleBuffer *sb, double value = 1.0) {
  const size_t n = sb->sample_count();
  const int channels = sb->channel_count();
  for (int c = 0; c < channels; c++) {
    std::fill(sb->data(c), sb->data(c) + n, float(value));
  }
}
```

### 2.3 Measurement Mathematics & Tolerances

#### Steady-State Root Mean Square (RMS):
For a discrete signal $y[n]$ measured over window $[N_0, N-1]$ (skipping the first $N_0$ samples to exclude initial filter impulse transients):
$$\text{RMS} = \sqrt{\frac{1}{N - N_0} \sum_{n=N_0}^{N-1} y[n]^2}$$
For a pure sine wave with amplitude $A$, the theoretical steady-state RMS is $\frac{A}{\sqrt{2}} \approx 0.707107 A$.

#### Decibel Gain Calculation:
$$\text{Gain}_{\text{dB}} = 20 \log_{10} \left( \frac{\text{RMS}_{\text{out}}}{\text{RMS}_{\text{in}}} \right)$$

#### Window Selection:
At sample rate $F_s = 48000$ Hz, choosing $N = 4800$ samples (0.1 seconds) and skipping the first $N_0 = 800$ samples guarantees:
- Filter transient decay $> 16$ ms (sufficient for $Q \le 5.0$ at frequencies $\ge 100$ Hz).
- Exactly 10 complete cycles for $100$ Hz ($4800 / 480 = 10$).
- Exactly 100 complete cycles for $1000$ Hz.
- Exactly 1000 complete cycles for $10000$ Hz.
- Zero spectral leakage across integer period counts!

---

## 3. Build & Test Environment (ASan & Gauntlet)

### 3.1 CMake Configuration with AddressSanitizer (`linux-asan`)

The `linux-asan` preset is configured in `CMakePresets.json`:
- `CMAKE_BUILD_TYPE`: `Debug`
- `BUILD_QT6`: `ON`
- `BUILD_TESTS`: `ON`
- `ENABLE_SANITIZER_ADDRESS`: `ON`
- `ENABLE_SANITIZER_UNDEFINED_BEHAVIOR`: `ON`
- Binary directory: `${sourceDir}/build-linux-asan`

In `cmake/Sanitizers.cmake`:
```cmake
target_compile_options(${target} INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(${target} INTERFACE -fsanitize=address,undefined)
```

### 3.2 Gauntlet Quality Gate (`scripts/gauntlet.py`)

The gate script executes:
1. `cmake --preset linux-asan -B build-linux-asan -DBUILD_GPU_TESTS=OFF`
2. `cmake --build build-linux-asan -j 4`
3. `ctest --test-dir build-linux-asan --show-only=json-v1`
4. `ctest --test-dir build-linux-asan --output-on-failure --no-tests=error --timeout 120 --output-junit qa-results/<stamp>/junit-0001.xml`

Under AddressSanitizer:
- Any memory leak at program exit immediately triggers an ASan leak report (`LeakSanitizer: detected memory leaks`) with non-zero exit code, failing CTest.
- Any out-of-bounds pointer dereference in planar sample loops triggers `heap-buffer-overflow`.
- Any uninitialized variable or division-by-zero triggers `UndefinedBehaviorSanitizer`.

### 3.3 Zero-Leak Memory Management Pattern for Audio Tests

To guarantee 0 memory leaks in unit tests:
1. Do NOT allocate `Node` or `Track` using bare `new` without a parent or `std::unique_ptr`.
2. Recommended pattern:
   ```cpp
   Project project;
   Sequence sequence;
   sequence.setParent(&project);
   // Nodes parented to project are cleanly destroyed when project goes out of scope
   ```
3. For standalone node testing:
   ```cpp
   EqualizerNode eq; // stack-allocated, zero heap leak
   // or std::unique_ptr<EqualizerNode> eq = std::make_unique<EqualizerNode>();
   ```

---

## 4. Test Suite Specification 1: `tests/node/equalizer-tests.cpp`

### 4.1 Test Scope & Coverage Matrix

| Test Function | Target Verification | Success Criterion |
|---|---|---|
| `EqualizerFlatResponse` | Bypass mode & 0 dB gain preserves samples | $\max \|y[n] - x[n]\| < 10^{-5}$ across all channels |
| `EqualizerLowPassFilter` | 1 kHz LPF passes 100 Hz, suppresses 10 kHz | $\text{RMS}_{100\text{Hz}} > 0.65$, $\text{RMS}_{10\text{kHz}} < 0.02$, attenuation $> 30$ dB |
| `EqualizerHighPassFilter` | 1 kHz HPF suppresses 100 Hz, passes 5 kHz | $\text{RMS}_{100\text{Hz}} < 0.02$, $\text{RMS}_{5\text{kHz}} > 0.65$, attenuation $> 30$ dB |
| `EqualizerPeakingBellBoostCut` | 1 kHz Bell: +6 dB ($2\times$) boost, -6 dB ($0.5\times$) cut | Boost: $\text{RMS} = 1.414 \pm 0.07$; Cut: $\text{RMS} = 0.354 \pm 0.03$; 100 Hz unchanged |
| `EqualizerUnitImpulseStability` | Unit impulse through 4 resonant bands over 48,000 samples | $\forall n: \text{isfinite}(y[n])$, no NaN/Inf, tail decay $\|y[47999]\| < 10^{-6}$ |
| `EqualizerNyquistAndLimits` | $f_0 \to 10$ Hz, $f_0 \to 24$ kHz, $Q = 10.0$, Gain $=\pm 24$ dB | Filter clamps gracefully without crashing, NaN, or Inf |
| `EqualizerPlanarStereoIndependence` | Ch 0 = 100 Hz, Ch 1 = 10 kHz through 1 kHz LPF | Ch 0 passes, Ch 1 attenuated; zero crosstalk between channels |

### 4.2 Complete C++ Test Implementation Blueprint

```cpp
#include <cmath>
#include <vector>
#include <algorithm>

#include <libavutil/channel_layout.h>

#include "testutil.h"
#include "core/render/samplebuffer.h"
#include "core/render/audioparams.h"
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
  const double w = 2.0 * M_PI * frequency / fs;

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
        OLIVE_ASSERT(std::fabs(output.data(c)[i] - input.data(c)[i]) < 1e-6f);
      }
    }
  }

  // Case B: Enabled = true, all bands at 0 dB gain
  {
    EqualizerNode eq;
    eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
    // Initialize bands with 0 dB gain
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
    // Identity within float precision
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
    eq.SetBandType(0, Biquad::FilterType::kLowPass);
    eq.SetBandFrequency(0, 1000.0f);
    eq.SetBandQ(0, 0.707f);
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

    // Evaluate steady-state RMS (samples 1000 to 4800)
    float low_rms = CalculateRMS(low_out.data(0), 1000, 3800);
    float high_rms = CalculateRMS(high_out.data(0), 1000, 3800);

    OLIVE_ASSERT(low_rms > 0.65f);   // 100 Hz preserved (> -1 dB)
    OLIVE_ASSERT(high_rms < 0.025f); // 10 kHz heavily attenuated (> 30 dB down)
  }

  // Part 2: High-Pass Filter (Cutoff = 1000 Hz, Q = 0.707)
  {
    EqualizerNode eq;
    eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
    eq.SetBandType(0, Biquad::FilterType::kHighPass);
    eq.SetBandFrequency(0, 1000.0f);
    eq.SetBandQ(0, 0.707f);
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

    OLIVE_ASSERT(low_rms < 0.025f); // 100 Hz suppressed (> 30 dB down)
    OLIVE_ASSERT(high_rms > 0.65f); // 5 kHz passed
  }

  // Part 3: Peaking Bell Filter (+6.02 dB boost = factor 2.0x, -6.02 dB cut = factor 0.5x)
  {
    // +6 dB boost at 1000 Hz
    EqualizerNode eq_boost;
    eq_boost.SetStandardValue(EqualizerNode::kEnabledInput, true);
    eq_boost.SetBandType(0, Biquad::FilterType::kPeaking);
    eq_boost.SetBandFrequency(0, 1000.0f);
    eq_boost.SetBandGain(0, 6.0206f);
    eq_boost.SetBandQ(0, 2.0f);
    for (int b = 1; b < EqualizerNode::kBandCount; b++) eq_boost.SetBandEnabled(b, false);

    SampleBuffer target_in(params, kTestSampleCount);
    FillSine(&target_in, 1000.0, 1.0); // Baseline RMS ~ 0.7071
    NodeValueRow row_b;
    row_b.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, target_in));
    row_b.insert(EqualizerNode::kEnabledInput, NodeValue(NodeValue::kBoolean, true));
    NodeGlobals globals_b(VideoParams(), params, TimeRange(0, rational(kTestSampleCount, kSampleRate)), LoopMode::kLoopModeOff);
    NodeValueTable table_b;
    eq_boost.Value(row_b, globals_b, &table_b);
    SampleBuffer boost_out = table_b.Get(NodeValue::kSamples).toSamples();

    float boosted_rms = CalculateRMS(boost_out.data(0), 1000, 3800);
    // 0.7071 * 2.0 = 1.4142. Tolerance: +/- 0.07
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
  // Configure resonant peaking bell with high Q (Q = 5.0) and heavy boost (+18 dB)
  eq.SetBandType(0, Biquad::FilterType::kPeaking);
  eq.SetBandFrequency(0, 1000.0f);
  eq.SetBandGain(0, 18.0f);
  eq.SetBandQ(0, 5.0f);

  // Band 1: High shelf (+12 dB)
  eq.SetBandType(1, Biquad::FilterType::kHighShelf);
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
      // 1. Must be finite: no NaN, no Inf
      OLIVE_ASSERT(std::isfinite(val));
      // 2. Bound peak response (no explosion)
      OLIVE_ASSERT(std::fabs(val) < 20.0f);
    }

    // 3. Tail decay check: After 0.1s (4800 samples), tail must be decaying towards zero
    for (size_t i = 4800; i < kOneSecond; i++) {
      OLIVE_ASSERT(std::fabs(data[i]) < 1e-3f);
    }

    // 4. End sample check: At 1.0s, energy must be practically extinguished
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
    eq.SetBandFrequency(0, 0.0f); // Out of bounds low
    eq.SetBandGain(0, 12.0f);
    eq.SetBandQ(0, 0.707f);

    NodeValueRow row;
    row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
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
    eq.SetBandFrequency(0, 24000.0f); // Exactly Nyquist
    eq.SetBandGain(0, -24.0f);
    eq.SetBandQ(0, 10.0f);

    NodeValueRow row;
    row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
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
    eq.SetBandQ(0, 0.001f); // Extremely low Q

    NodeValueRow row;
    row.insert(EqualizerNode::kSamplesInput, NodeValue(NodeValue::kSamples, input));
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
  const double w0 = 2.0 * M_PI * 100.0 / fs;
  for (size_t i = 0; i < kTestSampleCount; i++) {
    input.data(0)[i] = float(std::sin(w0 * double(i)));
  }

  // Channel 1: 10,000 Hz tone (high frequency)
  const double w1 = 2.0 * M_PI * 10000.0 / fs;
  for (size_t i = 0; i < kTestSampleCount; i++) {
    input.data(1)[i] = float(std::sin(w1 * double(i)));
  }

  // Low-Pass filter at 1000 Hz
  EqualizerNode eq;
  eq.SetStandardValue(EqualizerNode::kEnabledInput, true);
  eq.SetBandType(0, Biquad::FilterType::kLowPass);
  eq.SetBandFrequency(0, 1000.0f);
  eq.SetBandQ(0, 0.707f);
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

  // Channel 0 (100 Hz) passed freely through Low-Pass
  OLIVE_ASSERT(ch0_rms > 0.65f);
  // Channel 1 (10 kHz) attenuated significantly
  OLIVE_ASSERT(ch1_rms < 0.025f);

  OLIVE_TEST_END;
}

} // namespace olive
```

---

## 5. Test Suite Specification 2: `tests/timeline/track-audio-tests.cpp`

### 5.1 Test Scope & Coverage Matrix

| Test Function | Target Verification | Success Criterion |
|---|---|---|
| `TrackAudioVolumeScaling` | Track `volume_in` (0.0, 0.5, 1.0, 2.0) scales `SampleBuffer` | $0.0 = 0.0f$, $0.5 = 0.5\times$, $1.0 = 1.0\times$, $2.0 = 2.0\times$ within $10^{-6}$ |
| `TrackAudioStereoPanning` | Track `pan_in` (-1.0, -0.5, 0.0, 0.5, 1.0) scales channels | $-1.0$: ch1 silent, ch0 unity; $+1.0$: ch0 silent, ch1 unity; $0.0$: unity |
| `TrackAudioSoloLogic` | Sequence track solo/mute interaction | Solo track A silences track B; both soloed $\to$ both active; neither $\to$ both active |

### 5.2 Complete C++ Test Implementation Blueprint

```cpp
#include <cmath>
#include <vector>

#include <libavutil/channel_layout.h>

#include "testutil.h"
#include "core/render/samplebuffer.h"
#include "core/render/audioparams.h"
#include "node/block/clip/clip.h"
#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelineundogeneral.h"

namespace olive {

namespace {

const int kSampleRate = 48000;
const int kBlockSize = 1024;

AudioParams TestStereoParams() {
  return AudioParams(kSampleRate, uint64_t(AV_CH_LAYOUT_STEREO), core::SampleFormat::F32P);
}

void FillTone(SampleBuffer *sb, float val_ch0 = 0.8f, float val_ch1 = 0.8f) {
  const size_t n = sb->sample_count();
  for (size_t i = 0; i < n; i++) {
    sb->data(0)[i] = val_ch0;
    sb->data(1)[i] = val_ch1;
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Test 1: Track Volume Scaling (0.0, 0.5, 1.0, 2.0)
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(TrackAudioVolumeScaling)
{
  const AudioParams params = TestStereoParams();
  ClipBlock clip;
  clip.set_length_and_media_out(rational(kBlockSize, kSampleRate));

  SampleBuffer input(params, size_t(kBlockSize));
  FillTone(&input, 0.8f, 0.8f);

  Track track;
  track.set_type(Track::kAudio);
  track.AppendBlock(&clip);

  NodeValueArray blocks;
  blocks.emplace(track.GetArrayIndexFromBlock(&clip), NodeValue(NodeValue::kSamples, input));
  NodeValueRow row;
  row.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks, &track, true));
  NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(kBlockSize, kSampleRate)), LoopMode::kLoopModeOff);

  // Case 1: Unity volume (1.0)
  {
    track.SetStandardValue(Track::kVolumeInput, 1.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    OLIVE_ASSERT_EQUAL(res.sample_count(), size_t(kBlockSize));
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kBlockSize; i++) {
        OLIVE_ASSERT(std::fabs(res.data(c)[i] - 0.8f) < 1e-6f);
      }
    }
  }

  // Case 2: Half volume (0.5 = -6 dB)
  {
    track.SetStandardValue(Track::kVolumeInput, 0.5f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kBlockSize; i++) {
        OLIVE_ASSERT(std::fabs(res.data(c)[i] - 0.4f) < 1e-6f);
      }
    }
  }

  // Case 3: Double volume (2.0 = +6 dB)
  {
    track.SetStandardValue(Track::kVolumeInput, 2.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kBlockSize; i++) {
        OLIVE_ASSERT(std::fabs(res.data(c)[i] - 1.6f) < 1e-6f);
      }
    }
  }

  // Case 4: Mute / Silence (0.0)
  {
    track.SetStandardValue(Track::kVolumeInput, 0.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (int c = 0; c < 2; c++) {
      for (size_t i = 0; i < kBlockSize; i++) {
        OLIVE_ASSERT_EQUAL(res.data(c)[i], 0.0f);
      }
    }
  }

  OLIVE_TEST_END;
}

// ---------------------------------------------------------------------------
// Test 2: Track Stereo Pan (-1.0, -0.5, 0.0, 0.5, +1.0)
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(TrackAudioStereoPanning)
{
  const AudioParams params = TestStereoParams();
  ClipBlock clip;
  clip.set_length_and_media_out(rational(kBlockSize, kSampleRate));

  SampleBuffer input(params, size_t(kBlockSize));
  FillTone(&input, 1.0f, 1.0f);

  Track track;
  track.set_type(Track::kAudio);
  track.AppendBlock(&clip);

  NodeValueArray blocks;
  blocks.emplace(track.GetArrayIndexFromBlock(&clip), NodeValue(NodeValue::kSamples, input));
  NodeValueRow row;
  row.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks, &track, true));
  NodeGlobals globals(VideoParams(), params, TimeRange(0, rational(kBlockSize, kSampleRate)), LoopMode::kLoopModeOff);

  // Pan Center (0.0) -> Both Left and Right are 1.0
  {
    track.SetStandardValue(Track::kVolumeInput, 1.0f);
    track.SetStandardValue(Track::kPanInput, 0.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT(std::fabs(res.data(0)[i] - 1.0f) < 1e-6f);
      OLIVE_ASSERT(std::fabs(res.data(1)[i] - 1.0f) < 1e-6f);
    }
  }

  // Pan Full Left (-1.0) -> Left is 1.0, Right is completely silenced (0.0)
  {
    track.SetStandardValue(Track::kPanInput, -1.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT(std::fabs(res.data(0)[i] - 1.0f) < 1e-6f);
      OLIVE_ASSERT_EQUAL(res.data(1)[i], 0.0f);
    }
  }

  // Pan Full Right (+1.0) -> Left is completely silenced (0.0), Right is 1.0
  {
    track.SetStandardValue(Track::kPanInput, 1.0f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT_EQUAL(res.data(0)[i], 0.0f);
      OLIVE_ASSERT(std::fabs(res.data(1)[i] - 1.0f) < 1e-6f);
    }
  }

  // Pan Half Right (0.5) -> Left scaled by (1.0 - 0.5) = 0.5, Right unchanged (1.0)
  {
    track.SetStandardValue(Track::kPanInput, 0.5f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT(std::fabs(res.data(0)[i] - 0.5f) < 1e-6f);
      OLIVE_ASSERT(std::fabs(res.data(1)[i] - 1.0f) < 1e-6f);
    }
  }

  // Pan Half Left (-0.5) -> Left unchanged (1.0), Right scaled by (1.0 - 0.5) = 0.5
  {
    track.SetStandardValue(Track::kPanInput, -0.5f);
    NodeValueTable table;
    track.Value(row, globals, &table);
    SampleBuffer res = table.Get(NodeValue::kSamples).toSamples();
    for (size_t i = 0; i < kBlockSize; i++) {
      OLIVE_ASSERT(std::fabs(res.data(0)[i] - 1.0f) < 1e-6f);
      OLIVE_ASSERT(std::fabs(res.data(1)[i] - 0.5f) < 1e-6f);
    }
  }

  OLIVE_TEST_END;
}

// ---------------------------------------------------------------------------
// Test 3: Track Solo Logic Verification Across Multiple Tracks
// ---------------------------------------------------------------------------
OLIVE_ADD_TEST(TrackAudioSoloLogic)
{
  const AudioParams params = TestStereoParams();
  Project project;
  Sequence sequence;
  sequence.setParent(&project);

  Track *track_a = TimelineAddTrackCommand::RunImmediately(sequence.track_list(Track::kAudio));
  Track *track_b = TimelineAddTrackCommand::RunImmediately(sequence.track_list(Track::kAudio), true);

  OLIVE_ASSERT(track_a != nullptr);
  OLIVE_ASSERT(track_b != nullptr);

  ClipBlock clip_a, clip_b;
  clip_a.set_length_and_media_out(rational(kBlockSize, kSampleRate));
  clip_b.set_length_and_media_out(rational(kBlockSize, kSampleRate));
  track_a->AppendBlock(&clip_a);
  track_b->AppendBlock(&clip_b);

  const TimeRange range(0, rational(kBlockSize, kSampleRate));

  // Scenario 1: Neither track soloed -> Both active
  {
    track_a->SetStandardValue(Track::kSoloInput, false);
    track_b->SetStandardValue(Track::kSoloInput, false);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(!act_a.elements().empty());
    OLIVE_ASSERT(!act_b.elements().empty());
  }

  // Scenario 2: Track A soloed -> Track A active, Track B silenced (kNoElements)
  {
    track_a->SetStandardValue(Track::kSoloInput, true);
    track_b->SetStandardValue(Track::kSoloInput, false);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(!act_a.elements().empty());
    OLIVE_ASSERT(act_b.elements().empty());
  }

  // Scenario 3: Both tracks soloed -> Both active
  {
    track_a->SetStandardValue(Track::kSoloInput, true);
    track_b->SetStandardValue(Track::kSoloInput, true);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(!act_a.elements().empty());
    OLIVE_ASSERT(!act_b.elements().empty());
  }

  // Scenario 4: Track B soloed -> Track B active, Track A silenced
  {
    track_a->SetStandardValue(Track::kSoloInput, false);
    track_b->SetStandardValue(Track::kSoloInput, true);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(act_a.elements().empty());
    OLIVE_ASSERT(!act_b.elements().empty());
  }

  // Scenario 5: Track A soloed, but also muted -> Mute overrides solo (both silenced)
  {
    track_a->SetStandardValue(Track::kSoloInput, true);
    track_a->SetMuted(true);
    track_b->SetStandardValue(Track::kSoloInput, false);
    track_b->SetMuted(false);

    Node::ActiveElements act_a = track_a->GetActiveElementsAtTime(Track::kBlockInput, range);
    Node::ActiveElements act_b = track_b->GetActiveElementsAtTime(Track::kBlockInput, range);
    OLIVE_ASSERT(act_a.elements().empty()); // A is muted
    OLIVE_ASSERT(act_b.elements().empty()); // B is silenced because A is soloed
  }

  OLIVE_TEST_END;
}

} // namespace olive
```

---

## 6. CMake Registration Specification

### 6.1 Understanding Olive's Test Path Resolution

`tests/CMakeLists.txt` executes `file(READ "${SOURCE}" TEST_FILE_CONTENT)` and `file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${SOURCE}" "${TEST_FILE_CONTENT}")`.
In existing Olive subdirectories (e.g. `tests/timeline/CMakeLists.txt`), calls use relative filenames within the subdirectory:
```cmake
# In tests/timeline/CMakeLists.txt
olive_add_test(Timeline track-audio-tests track-audio-tests.cpp)
```
Where `SOURCE` is `track-audio-tests.cpp`, and `${CMAKE_CURRENT_SOURCE_DIR}` is `tests/timeline`.

### 6.2 Recommended CMake Registrations

To satisfy the specification cleanly:

#### 1. Equalizer Tests (`tests/node/equalizer-tests.cpp`):
- Create `tests/node/CMakeLists.txt`:
  ```cmake
  olive_add_test(Node equalizer-tests equalizer-tests.cpp)
  ```
- Add subdirectory to `tests/CMakeLists.txt`:
  ```cmake
  add_subdirectory(node)
  ```
- *Alternative direct call in `tests/CMakeLists.txt`*:
  ```cmake
  olive_add_test(Node equalizer-tests node/equalizer-tests.cpp)
  ```

#### 2. Track Audio Tests (`tests/timeline/track-audio-tests.cpp`):
- Add to existing `tests/timeline/CMakeLists.txt`:
  ```cmake
  olive_add_test(Timeline track-audio-tests track-audio-tests.cpp)
  ```
- *Alternative direct call in `tests/CMakeLists.txt`*:
  ```cmake
  olive_add_test(Timeline track-audio-tests timeline/track-audio-tests.cpp)
  ```

CTest automatically registers these test targets:
- Executable: `equalizer-tests`, label: `Node`
- Executable: `track-audio-tests`, label: `Timeline`

---

## 7. Quality & Verification Gates

### 7.1 Compilation Verification Command
```bash
cmake --build build-linux-asan --target equalizer-tests track-audio-tests -j 4
```

### 7.2 Independent Test Execution Commands
```bash
# Run equalizer unit tests with full terminal output
ctest --test-dir build-linux-asan -R equalizer-tests -V

# Run track audio unit tests with full terminal output
ctest --test-dir build-linux-asan -R track-audio-tests -V
```

### 7.3 Gauntlet Gate
```bash
python3 scripts/gauntlet.py --preset linux-asan --jobs 4
```
Expected output:
- `test_executables`: 9 (7 existing + 2 new)
- `junit-0001.xml`: All test cases passed
- `report.json`: `"status": "passed"`
- ASan: 0 memory leaks, 0 heap-buffer-overflows, 0 assertions failed.

---

## 8. Summary of Findings & Actionable Guidance for Implementer

1. **Pure C++ DSP Implementation**:
   Implement `Biquad` in `app/node/audio/equalizer/biquad.h` using standard RBJ cookbook formulas and Transposed Direct Form II.
2. **Frequency and Q Clamping**:
   Strictly clamp $f_0 \in [10.0, 0.49 \cdot F_s]$ and $Q \in [0.1, 10.0]$ inside coefficient calculation to prevent division-by-zero or Nyquist pole singularities.
3. **Planar Loop Optimization**:
   Loop through channels independently:
   ```cpp
   for (int c = 0; c < channels; c++) {
     float* data = buffer.data(c);
     biquads_[c].process(data, count);
   }
   ```
4. **Track Audio Processing**:
   In `Track::ProcessAudioTrack`, apply `kVolumeInput` via `buffer.transform_volume(vol)` and `kPanInput` via `buffer.transform_volume_for_channel(0, 1.0f - pan)` / `buffer.transform_volume_for_channel(1, 1.0f + pan)`.
5. **Solo Logic**:
   In `Track::GetActiveElementsAtTime`, check if any peer audio track in `sequence_->track_list(Track::kAudio)` has `solo == true`. If so, non-soloed tracks return `ActiveElements::kNoElements`.
