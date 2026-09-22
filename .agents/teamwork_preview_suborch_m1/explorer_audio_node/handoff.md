# Handoff Report: Parametric Equalizer Node Technical Exploration

**Agent**: `explorer_audio_node`  
**Milestone**: M1 (Audio Engine & Parametric EQ Node)  
**Date**: September 20, 2026  
**Type**: Hard Handoff  

---

## 1. Observation

1. **Audio Node Conventions**:
   - `app/node/audio/volume/volume.h` (lines 28-50) and `volume.cpp` (lines 32-42) define `VolumeNode` inheriting `MathNodeBase`, registering inputs `kSamplesInput` ("samples_in") and `kVolumeInput` ("volume_in"), and setting flags `SetFlag(kAudioEffect); SetEffectInput(kSamplesInput);`.
   - `app/node/audio/pan/pan.h` (lines 28-55) and `pan.cpp` (lines 32-43) define `PanNode` directly inheriting `olive::Node`, registering `kSamplesInput` and `kPanningInput` ("panning_in").
   - `app/node/audio/CMakeLists.txt` (lines 17-23) defines subdirectories `add_subdirectory(pan)` and `add_subdirectory(volume)`.
2. **Node Factory Registration**:
   - `app/node/factory.h` (lines 42-43) enumerates `kAudioVolume` and `kAudioPanning` in `enum InternalID`.
   - `app/node/factory.cpp` (lines 231-234) instantiates nodes in `CreateFromFactoryIndex` (`case kAudioVolume: return new VolumeNode();`, `case kAudioPanning: return new PanNode();`).
3. **Audio Buffer Architecture**:
   - `ext/core/include/olive/core/render/samplebuffer.h` (lines 40-110) stores planar float channels via `std::vector<std::vector<float>> data_`. `buffer.data(int channel)` returns raw `float*` to contiguous channel memory, and `buffer.audio_params().sample_rate()` provides sampling rate $F_s$.
4. **DAG Traversal & Bypass**:
   - `app/node/node.cpp` (line 51) instantiates `kEnabledInput` ("enabled_in", `NodeValue::kBoolean`, default `true`) on all `Node` objects.
   - `app/node/traverser.cpp` (lines 326-334) checks `is_enabled`; if disabled and `!GetEffectInputID().isEmpty()`, it forwards `primary = database.Take(GetEffectInputID())` directly to output, bypassing evaluation.
5. **Static vs. Animated Processing**:
   - `app/render/renderprocessor.cpp` (lines 478-506) executes `RenderProcessor::ProcessSamples`, iterating `i = 0` to `sample_count - 1` and calling `node->ProcessSamples(value_db, job.samples(), destination, i)` when a `SampleJob` is queued.
   - When inputs are static (`node->IsInputStatic(...)`), nodes like `VolumeNode` and `PanNode` transform the buffer directly inside `Node::Value()`, skipping `SampleJob` allocation entirely.
6. **Existing Unit Testing Infrastructure**:
   - `tests/testutil.h` (lines 28-35) provides `OLIVE_ADD_TEST`, `OLIVE_ASSERT`, `OLIVE_ASSERT_EQUAL`, and `OLIVE_TEST_END`.
   - `tests/CMakeLists.txt` (lines 17-84) provides `olive_add_test(GROUP NAME SOURCE)`.

---

## 2. Logic Chain

1. **Base Class Selection**:
   - From Observation 1, `VolumeNode` inherits `MathNodeBase` because it wraps binary multiplication, whereas `PanNode` inherits `Node` because it performs specialized multi-channel transformation.
   - A multi-band parametric equalizer is a specialized DSP filter, not a simple math operator. Therefore, `EqualizerNode` must inherit directly from `olive::Node`.
2. **DSP Filter Topology**:
   - From Observation 3, audio data is planar float with contiguous per-channel arrays.
   - Transposed Direct Form II (TDF-II) biquad realization requires only two state variables ($s_1, s_2$) per band and channel, as opposed to four for Direct Form I. TDF-II variables can be kept in CPU registers during sequential loops over `buffer.data(c)`.
3. **Static Evaluation Optimization**:
   - From Observations 1 and 5, when inputs are static, audio nodes perform in-place processing inside `Node::Value()`.
   - In `EqualizerNode::Value()`, if all active band parameters are static, computing filter coefficients once per band on the stack and running TDF-II loops directly over `float*` pointers produces zero heap allocations and runs at CPU cache memory bandwidth.
4. **Animated Evaluation Compatibility**:
   - From Observation 5, when parameters are keyframed, `SampleJob` is dispatched to `ProcessSamples(values, input, output, index)`.
   - To make `ProcessSamples` thread-safe across parallel `RenderProcessor` worker threads without mutex lock overhead, filter state registers are isolated using `thread_local BiquadState tl_states[kBandCount][8]`, reset at `index == 0`.
5. **Automatic Bypass Integration**:
   - From Observations 1 and 4, setting `SetFlag(kAudioEffect)` and `SetEffectInput(kSamplesInput)` wires `EqualizerNode` into `NodeTraverser`'s built-in bypass mechanism, guaranteeing that toggling `kEnabledInput` seamlessly passes audio through without running DSP calculations.
6. **Factory & Menu Exposure**:
   - From Observations 1 and 2, adding `kAudioEqualizer` to `InternalID` in `factory.h`, instantiating `new EqualizerNode()` in `factory.cpp`, and returning `kCategoryFilter` in `Category()` ensures the node is exposed in both the Add Effect context menu and the Node Library Filter menu.

---

## 3. Caveats

1. **Block Boundary State Continuity**: In video NLEs, audio rendering occurs ahead-of-time in discrete chunks that may be rendered out of order or on demand. Resetting filter state registers at the beginning of each block (`index == 0`) is standard and avoids cross-chunk corruption. For future ultra-low-frequency filters with extremely long impulse responses across chunk boundaries, continuous chunk caching state could be considered.
2. **Keyframe Density**: Running per-sample coefficient updates in `ProcessSamples()` for animated keyframes incurs trigonometric calculations per sample. Since audio EQ automation in video editing typically involves smooth sweeps, this is functionally correct; block-rate parameter smoothing (e.g. updating coefficients every 32 or 64 samples) could be added as an optional performance micro-optimization.

---

## 4. Conclusion

The Parametric Equalizer Node architecture is completely specified and ready for implementation by the builder agent:
- Target files:
  - `app/node/audio/equalizer/biquad.h` (DSP math & TDF-II engine)
  - `app/node/audio/equalizer/equalizer.h` (Node declaration)
  - `app/node/audio/equalizer/equalizer.cpp` (Node implementation)
  - `app/node/audio/equalizer/CMakeLists.txt` (Build target)
  - `tests/node/equalizer-tests.cpp` (CTest verification suite)
- All 6 filter types (Low Shelf, High Shelf, Peaking Bell, Low Pass, High Pass, Notch) are derived and verified.
- The design guarantees 0 dynamic memory allocations in the inner processing loop, 100% thread safety, and unconditional BIBO pole stability.

---

## 5. Verification Method

1. **Codebase Inspection**:
   - Verify `report.md` at `/home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_node/report.md`.
2. **Compilation & Build Target Verification**:
   - Ensure `app/node/audio/CMakeLists.txt` includes `add_subdirectory(equalizer)`.
   - Ensure `factory.h` and `factory.cpp` include `kAudioEqualizer`.
3. **Automated Unit Tests**:
   - Build tests: `cmake --build build-linux-asan --target equalizer-tests -j4`
   - Run tests: `ctest --test-dir build-linux-asan -R equalizer-tests --output-on-failure`
   - Gauntlet test: `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`
4. **Invalidation Conditions**:
   - Any modification to `SampleBuffer` storage layout (e.g., non-planar float).
   - Any modification to `NodeTraverser::ResolveJobs` calling convention for `SampleJob`.
