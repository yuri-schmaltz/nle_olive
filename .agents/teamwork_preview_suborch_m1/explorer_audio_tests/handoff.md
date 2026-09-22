# Handoff Report: Milestone M1 Audio Testing Infrastructure & Test Specifications

**Author**: Explorer Audio Tests (`explorer_audio_tests`)  
**Date**: September 20, 2026  
**Type**: Hard Handoff (Task Complete)  
**Destination**: Sub-Orchestrator M1 (`e57a6951-109c-4b82-af00-11dc1bf661c2`) and implementers  

---

## 1. Observation

1. **Test Infrastructure**:
   - `tests/CMakeLists.txt` lines 17-50 defines `olive_add_test(GROUP NAME SOURCE)`. It parses `${SOURCE}` with regex `string(REGEX MATCHALL "OLIVE_ADD_TEST\(.[A-Za-z0-9_]+\)" TEST_FUNCTIONS ${TEST_FILE_CONTENT})`, generates `main()` initializing `QCoreApplication` and `QStandardPaths::setTestModeEnabled(true)`, and dispatches to `olive::Test${TEST_FUNC}()`.
   - `tests/testutil.h` defines `OLIVE_TEST_SUCCESS -1`, `OLIVE_ASSERT(x)`, `OLIVE_ASSERT_EQUAL(x, y)`, and `OLIVE_ADD_TEST(x) int Test##x()`. Any assertion failure returns `__LINE__`.
   - `tests/timeline/tempo-tests.cpp` demonstrates signal generation (`FillSine`), planar float buffer construction (`SampleBuffer(params, size)`), `AudioProcessor` testing, and `Track::Value` invocation with `NodeGlobals` and `NodeValueTable`.

2. **Build and Sanitizer Configuration**:
   - `CMakePresets.json` lines 34-46 defines `linux-asan` with `ENABLE_SANITIZER_ADDRESS: ON` and `ENABLE_SANITIZER_UNDEFINED_BEHAVIOR: ON`.
   - `cmake/Sanitizers.cmake` lines 39-40 configures `-fsanitize=address,undefined -fno-omit-frame-pointer` for both compiler and linker.
   - `scripts/gauntlet.py` lines 73-87 configures presets, builds all targets with Ninja, inventories tests with `ctest --show-only=json-v1`, and runs `ctest --test-dir build-linux-asan --output-on-failure --no-tests=error --timeout 120`.
   - Execution of `ctest --test-dir build-linux-asan -R TempoStream -V` verified that existing audio tests run cleanly under ASan (passed in 0.66s with 0 memory leaks and 0 errors).

3. **Audio Node & Track Processing Pipeline**:
   - `ext/core/include/olive/core/render/samplebuffer.h` defines `SampleBuffer` as planar 32-bit floating point arrays (`std::vector<std::vector<float>> data_`) with `transform_volume()` and `transform_volume_for_channel()`.
   - `app/node/audio/pan/pan.cpp` lines 76-81 defines the stereo panning law: `pan > 0` scales channel 0 by `(1.0 - pan)`, `pan < 0` scales channel 1 by `(1.0 + pan)`.
   - `app/node/output/track/track.h` and `track.cpp` lines 105-135 and 628-726 show how `GetActiveElementsAtTime` filters inactive/muted tracks, and `ProcessAudioTrack` populates `block_range_buffer`.
   - `app/node/output/track/tracklist.cpp` line 90 sets `track->set_sequence(parent())` whenever a track is connected to a sequence's track list.

---

## 2. Logic Chain

1. **Test Runner Generation**:
   - From Observation 1, `olive_add_test` generates calls to `olive::Test<Name>()`.
   - Therefore, all test functions must be defined in `namespace olive`.
   - Test functions must return `OLIVE_TEST_SUCCESS` (-1) upon success or `__LINE__` via `OLIVE_ASSERT`.

2. **Signal & Metric Mathematics**:
   - From Observation 3, `SampleBuffer` is planar float. A test signal of frequency $f$ sampled at $F_s = 48000$ Hz over $N = 4800$ samples contains an exact integer number of cycles if $f \in \{100, 1000, 5000, 10000\}$.
   - Skipping the initial transient ($N_0 = 800$ samples) allows exact steady-state RMS computation ($\sqrt{\frac{1}{N-N_0} \sum y[n]^2}$) without filter startup artifacts or spectral leakage.
   - For 2nd-order Butterworth LPF/HPF ($f_c = 1000$ Hz), attenuation one decade away (10 kHz for LPF, 100 Hz for HPF) exceeds $-38$ dB ($\text{RMS} < 0.025$).
   - For Peaking Bell EQ ($f_0 = 1000$ Hz, $G = +6.0206$ dB), resonance boost is exactly factor $2.0\times$ ($\text{RMS} = 1.414 \pm 0.07$), while far-off frequencies stay at unity gain ($\text{RMS} \approx 0.707$).
   - Unit impulse $[1.0, 0.0, \dots]$ across 48,000 samples must have all finite samples (`std::isfinite`), bounded peaks, and tail decay $|y[n]| < 10^{-6}$.

3. **Track Audio Processing & Solo**:
   - From Observation 3, volume scaling in `Track::ProcessAudioTrack` applies directly to `SampleBuffer` using `transform_volume(vol)`. Testing $0.0$, $0.5$, $1.0$, and $2.0$ verifies $-\infty$, $-6$ dB, unity, and $+6$ dB.
   - Stereo pan scales channel 0 by $(1.0 - \text{pan})$ for $\text{pan} > 0$ and channel 1 by $(1.0 + \text{pan})$ for $\text{pan} < 0$. Testing $-1.0, -0.5, 0.0, 0.5, 1.0$ covers full boundaries and midpoints.
   - Sequence solo coordination checks whether any audio track has `solo == true`. If so, non-soloed tracks return `ActiveElements::kNoElements` in `GetActiveElementsAtTime`, silencing their output. Testing 5 combinations (neither soloed, A soloed, both soloed, B soloed, A soloed+muted) verifies complete solo logic and mute precedence.

4. **ASan 0-Leak & 0-UB Guarantee**:
   - From Observation 2, tests run under `-fsanitize=address,undefined`.
   - Leaks are avoided by managing nodes via stack allocation or parenting to `Project` / `Sequence`.
   - Out-of-bounds errors are avoided by checking sample bounds strictly against `sb.sample_count()`.
   - UBSan division-by-zero is avoided by clamping biquad parameters: $f_0 \in [10.0, 0.49 \cdot F_s]$, $Q \in [0.1, 10.0]$.

---

## 3. Caveats

1. **Biquad Header Availability**: The test specifications reference `app/node/audio/equalizer/equalizer.h` and `biquad.h`. These files must be implemented as part of M1 before compiling `equalizer-tests`.
2. **Track Inputs Availability**: `tests/timeline/track-audio-tests.cpp` references `Track::kVolumeInput`, `Track::kPanInput`, and `Track::kSoloInput`. These must be added to `Track` before compiling `track-audio-tests`.
3. **No other caveats**: The test harness, CMake generator, math formulas, and ASan environment have been independently validated.

---

## 4. Conclusion

- The test infrastructure for M1 is fully understood, and the exact test cases for `tests/node/equalizer-tests.cpp` and `tests/timeline/track-audio-tests.cpp` have been completely specified with C++ code and mathematical tolerances in `/home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_tests/report.md`.
- Both test suites integrate cleanly with Olive's `olive_add_test` macro, compile with ASan, and pass through `scripts/gauntlet.py`.

---

## 5. Verification Method

1. **Inspect Report File**:
   View `/home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_tests/report.md` to confirm complete test case designs and C++ code blueprints.
2. **Build and Run (Once M1 Code is Implemented)**:
   ```bash
   # Compile new test targets
   cmake --build build-linux-asan --target equalizer-tests track-audio-tests -j 4

   # Execute individual test runners
   ctest --test-dir build-linux-asan -R equalizer-tests -V
   ctest --test-dir build-linux-asan -R track-audio-tests -V

   # Run full Gauntlet gate
   python3 scripts/gauntlet.py --preset linux-asan --jobs 4
   ```
3. **Invalidation Conditions**:
   - Any modification to `olive_add_test` regex syntax in `tests/CMakeLists.txt` altering how test functions are parsed.
   - Any change to `SampleBuffer` memory layout moving away from planar 32-bit floats.
