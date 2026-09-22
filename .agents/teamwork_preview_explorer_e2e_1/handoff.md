# Handoff Report: Comprehensive Opaque-Box E2E Test Suite Architecture & Specification

**Agent**: `teamwork_preview_explorer_e2e_1` (E2E Test Architecture Explorer)  
**Parent Agent**: `teamwork_preview_suborch_e2e` (`84402d74-b7ee-4aee-b377-b220077c8306`)  
**Date**: 2026-09-20  
**Handoff Type**: Hard Handoff (Investigation & Test Suite Design Complete)

---

## 1. Observation

### 1.1 Existing Test Subsystem and CMake Configuration
1. **Directory Structure & Registration (`tests/CMakeLists.txt`)**:
   - `tests/CMakeLists.txt` defines the custom CMake function `olive_add_test(GROUP NAME SOURCE [GPU])`.
   - The function automatically generates a test harness `main(int argc, char** argv)` that initializes `QCoreApplication app(argc, argv)` (or `QGuiApplication` if `GPU` is passed), enables test mode (`QStandardPaths::setTestModeEnabled(true)`), iterates over all functions registered with `OLIVE_ADD_TEST(...)`, executes them, and returns exit code `0` on success or `1` on assertion failure.
   - The test executable links against the object library `$<TARGET_OBJECTS:libolive-editor>` (which compiles all Olive application sources except `main.cpp`) and `${OLIVE_LIBRARIES}`.
   - Tests are registered in CTest via `add_test(${NAME} ${NAME})` with a default timeout of 120 seconds and labeled with `${GROUP}` (`set_tests_properties(${test_name} PROPERTIES TIMEOUT 120 LABELS "${GROUP}")`).
   - Existing test suites include: `tests/compositing/`, `tests/export/`, `tests/general/`, `tests/project/`, `tests/render/`, and `tests/timeline/`. There is currently no `tests/e2e/` directory.

2. **Test Assertions (`tests/testutil.h`)**:
   - Provides minimal, zero-overhead test macros:
     - `OLIVE_TEST_SUCCESS`: `-1`
     - `OLIVE_ASSERT(x)`: `if (!(x)) return __LINE__;`
     - `OLIVE_ASSERT_EQUAL(x, y)`: `if (x != y) { std::cout << " - Equal assert failed: " << x << " != " << y; return __LINE__; } void()`
     - `OLIVE_TEST_END`: `return OLIVE_TEST_SUCCESS;`
     - `OLIVE_ADD_TEST(x)`: `int Test##x()`

3. **Gauntlet Gate & Sanitizer Integration (`scripts/gauntlet.py`, `CMakePresets.json`)**:
   - `scripts/gauntlet.py` is the project's non-negotiable quality gate. It runs with `--preset linux-asan --jobs 4`.
   - In `CMakePresets.json`, `linux-asan` sets `BUILD_TESTS: ON`, `CMAKE_BUILD_TYPE: Debug`, `ENABLE_SANITIZER_ADDRESS: ON`, and `ENABLE_SANITIZER_UNDEFINED_BEHAVIOR: ON`.
   - `gauntlet.py` executes:
     1. `cmake --preset <preset> -B <build> -DBUILD_GPU_TESTS=OFF`
     2. `cmake --build <build> -j <jobs>`
     3. `ctest --test-dir <build> --output-on-failure --no-tests=error --timeout 120 --output-junit <junit.xml>`
     4. If `--install`: verifies installation prefix and binary version.
   - Crucially, `ctest` runs **all** registered tests. Any test target added to `tests/` will automatically be executed by `gauntlet.py` under AddressSanitizer and UndefinedBehaviorSanitizer, requiring **0 memory leaks and 0 assertion failures**.

4. **Python & Environment Capabilities**:
   - Python 3.12.3 is installed.
   - `pytest` is **not** installed in the environment (`ModuleNotFoundError: No module named 'pytest'`), but Python's standard library `unittest`, `subprocess`, `argparse`, `json`, `pathlib`, `hashlib`, and `xml` are fully available.
   - System `ffmpeg` is available (detected via `tests/export/CMakeLists.txt:1` `find_program(OLIVE_TEST_FFMPEG ffmpeg)`).

### 1.2 Feature Implementation Profiles (from Surveys and Codebase)
1. **Feature 1: Parametric Equalizer Node** (`app/node/audio/equalizer/`):
   - Implements Robert Bristow-Johnson (RBJ) Audio EQ Cookbook multi-band biquad IIR filters (Low Shelf, High Shelf, Peaking Bell, Low Pass, High Pass) operating on planar 32-bit floating-point `olive::core::SampleBuffer`. Inputs: `kSamplesInput` ("samples_in"), `kBandsInput` ("bands_in"), `kEnabledInput` ("enabled_in").
2. **Feature 2: Track Audio Controls** (`app/node/output/track/`):
   - Native inputs on `Track`: `kVolumeInput` ("volume_in", float, default 1.0), `kPanInput` ("pan_in", float, default 0.0), `kSoloInput` ("solo_in", bool, default false), `kMutedInput` ("muted_in", bool). Processed in `Track::ProcessAudioTrack`.
3. **Feature 3: Track Audio Mixer Panel** (`app/panel/audiomixer/`, `app/widget/audiomixer/`):
   - Dockable Qt6 `AudioMixerPanel` with vertical faders, pan dials, solo/mute toggles, master bus fader, and track strips reflecting active sequence tracks.
4. **Feature 4: Thread-Safe VU Metering**:
   - Master levels updated via lock-free `std::atomic<float>` registers during audio output callback; per-track levels calculated from `waveform_cache()`; ballistics decay implemented via decay coefficient ($\approx 20$ dB/sec).
5. **Feature 5: Asynchronous Scene Cut Analysis** (`app/task/scenecut/`):
   - `SceneCutDetector` computes 256-bin YUV luma histograms and normalized $L_1$ frame differences. `SceneCutTask` runs asynchronously on `TaskManager` CPU thread pool using dedicated `AVFormatContext`/`AVCodecContext`, emitting `SceneCutCompleted(QVector<rational>)`. Supports atomic `Cancel()`.
6. **Feature 6: Timeline Auto-Split** (`app/timeline/timelineundosplit.h`, `.cpp`):
   - `BlockSplitPreservingLinksCommand(blocks, times)`: Sorts cut times chronologically, splits video and audio blocks via `BlockSplitCommand`, relinks newly created blocks via `NodeLinkCommand`, and supports full `undo_now()` and `redo_now()`.
7. **Feature 7: Final Cut Pro 7 XML Interchange** (`app/task/project/fcpxml/`):
   - `LoadFCPXMLTask` & `SaveFCPXMLTask` use Qt `QXmlStreamReader` and `QXmlStreamWriter` with zero external dependencies. Preserves sequences, tracks, clips, in/out points, transitions, markers, and `<link>` dual video/audio associations.
8. **Feature 8: OpenTimelineIO Hardening** (`app/task/project/saveotio/`, `loadotio/`):
   - Resolves transition overwrite bug and memory leak at `saveotio.cpp:180`, adds marker serialization (`otio::Marker`), and serializes clip speed adjustments (`otio::LinearTimeWarp`).
9. **Feature 9: Main Menu Export Wiring** (`app/window/mainwindow/mainmenu.cpp`):
   - Wires "Export -> Final Cut Pro 7 XML (*.xml)" and "Export -> OpenTimelineIO (*.otio)" actions into `MainMenu::file_export_menu_`.
10. **Feature 10: Linux AppImage Packaging** (`packaging/linux/build_appimage.sh`, `app/packaging/linux/AppRun`):
    - Reproducible bundling script packaging Qt6 platform plugins (`libqxcb.so`), FFmpeg, OCIO, OIIO, and portable `AppRun` configuring `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, and `XDG_DATA_DIRS`.
11. **Feature 11: Linux Flatpak Packaging** (`packaging/flatpak/org.olivevideoeditor.Olive.json`):
    - Manifest targeting KDE Application Runtime 6.8+ (`org.kde.Platform` / `org.kde.Sdk`), with isolated sandbox permissions (`--socket=x11`, `--socket=wayland`, `--socket=pulseaudio`, `--device=dri`) and complete build recipes for external dependencies.

---

## 2. Logic Chain

### 2.1 Test Runner Architecture Strategy
1. **CTest Direct Integration for Core Engine & Gauntlet Compliance**:
   - Because `scripts/gauntlet.py` strictly gates code using `ctest --test-dir build-linux-asan`, all C++ E2E tests must be registered directly in CTest via `olive_add_test(E2E ...)` in `tests/e2e/CMakeLists.txt`.
   - This ensures that AddressSanitizer and UndefinedBehaviorSanitizer continuously instrument every test during `gauntlet.py` runs, verifying zero memory leaks, zero buffer overflows, and zero assertion failures.
   - Tests execute headlessly without requiring an X11/Wayland display or GPU context (using `QCoreApplication` or `-platform offscreen`).

2. **Standalone Python E2E Orchestrator (`tests/e2e/run_e2e.py`)**:
   - Since `pytest` is not installed, the Python test runner must use the standard library `unittest` module and `subprocess`.
   - `run_e2e.py` serves as a unified entry point for developers and CI:
     - Validates packaging artifacts (AppImage shell script syntax, AppRun environment variable exports, Flatpak manifest JSON syntax and schema).
     - Manages on-the-fly generation and cleanup of synthetic test media.
     - Invokes the compiled C++ E2E test executables (or CTest directly with `-L E2E`).
     - Aggregates results across Tier 1, Tier 2, Tier 3, and Tier 4, producing JUnit XML (`qa-results/junit-e2e.xml`) and formatted CLI reports.

3. **Zero-Binary-Bloat Synthetic Media Generation**:
   - Checking in binary video files (.mp4) and audio files (.wav) into git causes repository bloat.
   - Instead, all test media is generated on-the-fly:
     - **Audio Media**: Synthesized purely in-memory as `olive::core::SampleBuffer` (using trigonometric sine generation, white noise, and unit impulses $\delta[n]$), matching the pattern established in `tests/timeline/tempo-tests.cpp`.
     - **Video Media**: Generated on-the-fly via system FFmpeg (`lavfi` color test pattern filters, e.g. `color=c=red:d=3`, `color=c=blue:d=4` concatenated together) into a temporary directory (`QTemporaryDir` or Python `tempfile.TemporaryDirectory`). This produces deterministic cuts at exact timestamps ($t = 3.0$s, $t = 7.0$s) in $<0.5$ seconds with zero git footprint.
     - **Timeline Project Files**: Formatted in-memory using C++ raw string literals (`R"xml(...)xml"`) or built programmatically via the Olive Core object model (`Project`, `Sequence`, `Track`, `ClipBlock`).

---

## 3. Caveats

1. **OpenTimelineIO Optional Dependency**:
   - OTIO is conditionally compiled via `#ifdef USE_OTIO`. In environments where `OpenTimelineIO_FOUND` is false, OTIO tests must gracefully report `SKIPPED` or verify graceful fallback rather than failing the build. FCP7 XML tests remain 100% active on all platforms because they use native Qt XML streams.
2. **GPU Headless Execution**:
   - `gauntlet.py` runs with `-DBUILD_GPU_TESTS=OFF` by default. All E2E tests for audio, scene cut detection, timeline auto-split, and interchange must run completely headlessly in CPU RAM, without requiring OpenGL display initialization.
3. **AppImage & Flatpak Sandbox Execution**:
   - Packaging validation in E2E focuses on structural validation, shell syntax checking (`bash -n`), manifest JSON schema validation, dependency resolution, and installed binary version inspection. Running a full AppImage build or full Flatpak build inside Gauntlet would require container privileges and excessive build time (30+ minutes); structural and recipe linting in E2E guarantees packaging correctness within seconds.

---

## 4. Conclusion & Concrete E2E Test Suite Specification

### 4.1 Test Directory Structure & Files
The proposed E2E test suite in `tests/e2e/` consists of:

```
tests/e2e/
├── CMakeLists.txt                 # Registers E2E test executables & python runner in CTest
├── run_e2e.py                     # Standalone Python E2E runner (unittest + CLI)
├── test_packaging.py              # Packaging verification (AppImage script, AppRun, Flatpak JSON)
├── e2e_fixtures.h                 # Synthetic media generators (in-memory audio, video, XML)
├── e2e_audio_tests.cpp            # Tiers 1-3 for Parametric EQ, Track Controls, Mixer, VU Meters
├── e2e_scenecut_tests.cpp         # Tiers 1-3 for Scene Cut Detection & Timeline Auto-Split
├── e2e_interchange_tests.cpp      # Tiers 1-3 for FCP7 XML, OTIO, and Menu Export Wiring
└── e2e_workflow_tests.cpp          # Tier 4 Real-World End-to-End Multi-Step Scenarios
```

---

### 4.2 Detailed Test Specification: Tier 1 (Feature Coverage — Happy Paths)
*Requirement: $\ge 5$ tests per feature for all 11 features ($55$ test cases).*

#### Feature 1: Parametric Equalizer Node (Audio DSP)
- **T1.01.01 — Peaking Bell Filter Center Gain**:
  - *Input*: Planar 48 kHz stereo `SampleBuffer` with 1 kHz sine tone at 0.5 amplitude.
  - *Setup*: `EqualizerNode` configured with Band 1: Peaking Bell, $f_0 = 1000$ Hz, $Q = 1.0$, Gain = $+6.0$ dB.
  - *Assertion*: Output amplitude at 1 kHz equals $0.5 \times 10^{6/20} \approx 0.9976$ ($\pm 1\%$).
- **T1.01.02 — Low Shelf Filter Attenuation**:
  - *Input*: `SampleBuffer` containing mixed 100 Hz and 5 kHz sine tones.
  - *Setup*: Low Shelf filter at $f_0 = 200$ Hz, Gain = $-12.0$ dB.
  - *Assertion*: 100 Hz component attenuated by $12$ dB ($\approx 0.25\times$), while 5 kHz component remains within $0.5$ dB of unity.
- **T1.01.03 — High Shelf Filter Boost**:
  - *Input*: `SampleBuffer` containing mixed 1 kHz and 12 kHz sine tones.
  - *Setup*: High Shelf filter at $f_0 = 8000$ Hz, Gain = $+6.0$ dB.
  - *Assertion*: 12 kHz component boosted by $+6$ dB, while 1 kHz component remains unaffected.
- **T1.01.04 — Low Pass & High Pass Filters**:
  - *Input*: White noise buffer or multi-frequency sweep.
  - *Setup*: Low Pass at 1 kHz ($Q = 0.707$).
  - *Assertion*: Stopband frequency at 4 kHz attenuated by $>20$ dB. High Pass at 1 kHz attenuates 200 Hz by $>20$ dB.
- **T1.01.05 — Bypass / Enabled Switch**:
  - *Input*: Arbitrary complex audio buffer.
  - *Setup*: `EqualizerNode` with 5 active aggressive bands, but `kEnabledInput` set to `false`.
  - *Assertion*: Output buffer is bit-for-bit identical to input buffer (`memcmp == 0`).

#### Feature 2: Track Audio Controls (Volume, Pan, Solo)
- **T1.02.01 — Track Volume Scaling**:
  - *Input*: Track with 1 kHz sine tone at $1.0$ amplitude.
  - *Setup*: Set `kVolumeInput` to $0.5$ (-6 dB).
  - *Assertion*: `Track::ProcessAudioTrack` yields output with maximum peak $0.50$ ($\pm 0.001$).
- **T1.02.02 — Track Hard Panning (Left/Right)**:
  - *Input*: Stereo buffer on Track.
  - *Setup*: Set `kPanInput` to $-1.0$ (Full Left).
  - *Assertion*: Channel 0 has full signal; Channel 1 contains pure silence ($0.0$).
- **T1.02.03 — Track Center Panning Balance**:
  - *Input*: Mono or stereo input on Track.
  - *Setup*: Set `kPanInput` to $0.0$ (Center).
  - *Assertion*: Channel 0 and Channel 1 amplitudes are exactly equal.
- **T1.02.04 — Track Single Solo Isolation**:
  - *Input*: Sequence with 3 audio tracks (A1, A2, A3) playing distinct tones (400, 800, 1200 Hz).
  - *Setup*: A1 `solo = true`, A2 `solo = false`, A3 `solo = false`.
  - *Assertion*: Sequence master audio contains only 400 Hz; A2 and A3 contribute zero energy.
- **T1.02.05 — Track Multi-Solo Summation**:
  - *Input*: 3 audio tracks (A1, A2, A3).
  - *Setup*: A1 `solo = true`, A2 `solo = true`, A3 `solo = false`.
  - *Assertion*: Sequence master audio contains A1 + A2; A3 remains completely muted.

#### Feature 3: Track Audio Mixer Panel (UI & Property Binding)
- **T1.03.01 — Mixer Track Strip Synchronization**:
  - *Input*: Sequence initialized with 2 audio tracks.
  - *Execution*: Add 2 more audio tracks via `TimelineAddTrackCommand`.
  - *Assertion*: `AudioMixerPanel` updates dynamically, displaying exactly 4 track channel strips plus 1 master bus strip.
- **T1.03.02 — Fader to Track Volume Binding**:
  - *Input*: Audio track strip in `AudioMixerPanel`.
  - *Execution*: Move track fader to -10 dB.
  - *Assertion*: Corresponding `Track` property `kVolumeInput` reflects value $10^{-10/20} \approx 0.3162$, and undo command is registered in `UndoStack`.
- **T1.03.03 — Pan Dial Binding**:
  - *Input*: Track strip pan knob.
  - *Execution*: Adjust pan knob to $+0.75$ (Right).
  - *Assertion*: Track `kPanInput` updates to $+0.75$.
- **T1.03.04 — Solo Button Toggle**:
  - *Input*: Track strip Solo button clicked.
  - *Assertion*: Track `kSoloInput` flips from `false` to `true`, and button visual state updates to active yellow.
- **T1.03.05 — Master Bus Fader Control**:
  - *Input*: Master strip fader.
  - *Execution*: Set master fader to -6 dB.
  - *Assertion*: Sequence master output scales overall summed mix by 0.5.

#### Feature 4: Thread-Safe VU Metering (Atomic Registers & Ballistics)
- **T1.04.01 — Peak Level Capture**:
  - *Input*: Full scale audio pulse ($1.0$ peak).
  - *Execution*: Process chunk in audio thread callback.
  - *Assertion*: Master VU atomic peak register immediately reads $1.0$ (0 dBFS).
- **T1.04.02 — Ballistics Decay Rate**:
  - *Input*: Audio stream silenced after 0 dBFS signal.
  - *Execution*: Advance simulated ballistics timer by 1.0 second.
  - *Assertion*: Meter displayed level drops by $\approx 20$ dB ($\pm 2$ dB) matching IEC 60268-10 standards.
- **T1.04.03 — Per-Track RMS Level Calculation**:
  - *Input*: Sine wave with amplitude $0.7071$ (-3 dBFS).
  - *Assertion*: Track RMS register calculates $0.7071 / \sqrt{2} \approx 0.50$ (-6 dB RMS).
- **T1.04.04 — Multi-Threaded Concurrent Read/Write**:
  - *Input*: High-frequency audio thread writing atomic peak levels (1000 updates/sec) and GUI thread polling at 60 Hz across 8 worker threads.
  - *Assertion*: Zero data races detected under TSan/ASan; zero deadlocks; atomic load operations return valid float values in $[0.0, 1.0]$.
- **T1.04.05 — Clip Indicator Trigger**:
  - *Input*: Audio signal with peak $> 1.0$ (+1 dBFS).
  - *Assertion*: Overload/clip atomic flag is set to `true` and persists until reset timer expires.

#### Feature 5: Asynchronous Scene Cut Analysis (Detector & Task)
- **T1.05.01 — Hard Cut Detection on Synthetic Video**:
  - *Input*: Synthetic 10s video with known scene transitions at $t = 3.0$s and $t = 7.0$s.
  - *Execution*: Run `SceneCutDetector` on decoded YUV frames.
  - *Assertion*: Detected cut timestamps exactly match $[3.0, 7.0]$ ($\pm 1$ frame).
- **T1.05.02 — Luma Histogram Difference Calculation**:
  - *Input*: Two consecutive frames: Frame A (solid black, $Y=16$) and Frame B (solid white, $Y=235$).
  - *Assertion*: 256-bin histogram normalized $L_1$ distance equals $1.0$ (maximum difference).
- **T1.05.03 — Asynchronous Task Execution in TaskManager**:
  - *Input*: Instantiate `SceneCutTask` with test media path.
  - *Execution*: Enqueue task into `TaskManager::instance()`.
  - *Assertion*: Task transitions to Running, emits monotonically increasing `ProgressChanged(double)` between 0.0 and 1.0, and finishes with `Finished(true)`.
- **T1.05.04 — Task Completion Signal Emission**:
  - *Input*: `SceneCutTask` completes analysis.
  - *Assertion*: `SceneCutCompleted(QVector<rational>)` is received on the GUI thread containing valid cut timestamps.
- **T1.05.05 — Graceful Task Cancellation**:
  - *Input*: Running `SceneCutTask` processing a large file.
  - *Execution*: Call `task->Cancel()` at 50% progress.
  - *Assertion*: Worker thread halts promptly ($<100$ ms), finishes with status `false`, and frees all FFmpeg contexts with 0 ASan memory leaks.

#### Feature 6: Timeline Auto-Split (BlockSplitPreservingLinksCommand)
- **T1.06.01 — Multi-Point Split on Video Clip**:
  - *Input*: 10-second video `ClipBlock` on Video Track 1.
  - *Execution*: Execute `BlockSplitPreservingLinksCommand({clip}, {rational(3), rational(7)})`.
  - *Assertion*: Video Track 1 contains exactly 3 blocks: $[0, 3]$, $[3, 7]$, $[7, 10]$.
- **T1.06.02 — Linked Audio/Video Simultaneous Split**:
  - *Input*: 10-second video `ClipBlock` linked to 10-second audio `ClipBlock`.
  - *Execution*: Execute `BlockSplitPreservingLinksCommand({clip_v, clip_a}, {rational(4)})`.
  - *Assertion*: Both video and audio tracks split at $t = 4$. New video block $[4, 10]$ is linked to new audio block $[4, 10]$.
- **T1.06.03 — Full Undo Fidelity**:
  - *Input*: Timeline after successful 3-way split.
  - *Execution*: Call `undo_now()`.
  - *Assertion*: Both tracks revert to a single unbroken 10-second clip; original links are completely restored.
- **T1.06.04 — Full Redo Fidelity**:
  - *Input*: Timeline after Undo.
  - *Execution*: Call `redo_now()`.
  - *Assertion*: Timeline returns to the exact split state with 3 blocks on each track and verified links.
- **T1.06.05 — Media In/Out Point Consistency**:
  - *Input*: Clip with `media_in = 5`, `media_out = 15`. Split at sequence time $+3$.
  - *Assertion*: First block has `media_in = 5, media_out = 8`; second block has `media_in = 8, media_out = 15`.

#### Feature 7: Final Cut Pro 7 XML Interchange (Load/Save Tasks)
- **T1.07.01 — Export Basic Sequence to FCP7 XML**:
  - *Input*: Sequence with 1 video track, 1 audio track, 2 clips.
  - *Execution*: Run `SaveFCPXMLTask` to temporary file.
  - *Assertion*: File contains valid XML, `<xmeml version="5">`, `<sequence>`, `<timebase>`, and `<clipitem>` elements.
- **T1.07.02 — Import FCP7 XML to Olive Project**:
  - *Input*: Valid FCP7 XML file.
  - *Execution*: Run `LoadFCPXMLTask`.
  - *Assertion*: Returns valid `Project` containing `Sequence` with identical track counts and clip lengths.
- **T1.07.03 — Round-Trip Track & Clip In/Out Fidelity**:
  - *Input*: Sequence with clips at non-zero in/out and start/end points.
  - *Execution*: Save to FCP7 XML $\to$ Load from FCP7 XML.
  - *Assertion*: Clip start, end, in, out frame numbers match original values with 0 frame drift.
- **T1.07.04 — Marker Preservation in FCP7 XML**:
  - *Input*: Sequence containing timeline markers with names, comments, and color IDs.
  - *Execution*: FCP7 XML roundtrip.
  - *Assertion*: All markers are restored at exact timestamps with matching labels.
- **T1.07.05 — Video/Audio Link Preservation**:
  - *Input*: Video clip linked to Audio clip.
  - *Execution*: FCP7 XML roundtrip.
  - *Assertion*: Re-imported video and audio clips share corresponding `<link>` IDs and remain linked on the Olive timeline.

#### Feature 8: OpenTimelineIO Hardening (Save/Load OTIO)
- **T1.08.01 — Transition Overwrite Bug Fix Verification**:
  - *Input*: Sequence containing adjacent clips with a Cross Dissolve transition.
  - *Execution*: Run `SaveOTIOTask`.
  - *Assertion*: Resulting OTIO JSON contains populated `Transition` object with valid `in_offset` and `out_offset` (no blank transition overwrite or memory leak).
- **T1.08.02 — OTIO Round-Trip with Transitions**:
  - *Input*: Sequence with cross-dissolve transitions.
  - *Execution*: Save to `.otio` $\to$ Load from `.otio`.
  - *Assertion*: Transitions are reconstructed on the timeline with identical cut placement and duration.
- **T1.08.03 — OTIO Timeline Marker Serialization**:
  - *Input*: Sequence with 3 markers.
  - *Execution*: Save to `.otio`.
  - *Assertion*: JSON contains `markers` array on the timeline item with matching rational times.
- **T1.08.04 — OTIO Clip Speed Serialization**:
  - *Input*: Clip with speed set to 2.0x (`LinearTimeWarp`).
  - *Execution*: Save to `.otio` $\to$ Load from `.otio`.
  - *Assertion*: Clip length and playback speed factor 2.0x are preserved.
- **T1.08.05 — OTIO ASan Zero-Leak Cleanliness**:
  - *Input*: Complex multi-track sequence.
  - *Execution*: Execute `SaveOTIOTask` and `LoadOTIOTask` under AddressSanitizer.
  - *Assertion*: Memory report indicates 0 heap memory leaks.

#### Feature 9: Main Menu Export Wiring
- **T1.09.01 — Export FCP7 XML Menu Action Existence**:
  - *Assertion*: `MainMenu::file_export_menu_` contains an enabled action for "Final Cut Pro 7 XML (*.xml)".
- **T1.09.02 — Export OTIO Menu Action Existence**:
  - *Assertion*: `MainMenu::file_export_menu_` contains an action for "OpenTimelineIO (*.otio)".
- **T1.09.03 — Menu Action Signal Connection**:
  - *Execution*: Trigger FCP7 XML action programmatically.
  - *Assertion*: Slot is invoked and initiates file export dialog/task.
- **T1.09.04 — Dynamic Enabling/Disabling Based on Project State**:
  - *Execution*: Close all open sequences.
  - *Assertion*: Export actions are disabled when no active sequence is selected.
- **T1.09.05 — OTIO Menu Availability Conditional Check**:
  - *Assertion*: If compiled without OTIO, action is either hidden or displays an informative disabled tooltip.

#### Feature 10: Linux AppImage Packaging
- **T1.10.01 — AppRun Script Shell Syntax**:
  - *Execution*: Run `bash -n app/packaging/linux/AppRun`.
  - *Assertion*: Exit code 0 (valid bash syntax).
- **T1.10.02 — AppRun Environment Variable Exports**:
  - *Inspection*: Parse `app/packaging/linux/AppRun`.
  - *Assertion*: Script explicitly exports `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, and `XDG_DATA_DIRS`.
- **T1.10.03 — Build AppImage Script Syntax**:
  - *Execution*: Run `bash -n packaging/linux/build_appimage.sh`.
  - *Assertion*: Exit code 0.
- **T1.10.04 — Desktop File & Icon Integration**:
  - *Inspection*: Check packaging directory for `org.olivevideoeditor.Olive.desktop` and PNG icons.
  - *Assertion*: Desktop file passes `desktop-file-validate` (or regex linting) with valid Exec and Icon fields.
- **T1.10.05 — AppImage Installed Binary Verification**:
  - *Execution*: Staged binary runs `--version`.
  - *Assertion*: Output matches `olive::kAppVersionLong` with exit code 0.

#### Feature 11: Linux Flatpak Packaging
- **T1.11.01 — Flatpak Manifest JSON Syntax**:
  - *Execution*: Parse `packaging/flatpak/org.olivevideoeditor.Olive.json` via Python `json.loads`.
  - *Assertion*: Clean JSON parsing with no syntax errors.
- **T1.11.02 — Flatpak App ID & Command**:
  - *Assertion*: `app-id == "org.olivevideoeditor.Olive"` and `command == "olive-editor"`.
- **T1.11.03 — Flatpak Runtime & SDK Specifications**:
  - *Assertion*: `runtime == "org.kde.Platform"` and `sdk == "org.kde.Sdk"` with runtime version $\ge 6.8$.
- **T1.11.04 — Flatpak Finish-Args Sandbox Permissions**:
  - *Assertion*: Manifest contains `--share=ipc`, `--socket=x11`, `--socket=wayland`, `--socket=pulseaudio`, and `--device=dri`.
- **T1.11.05 — Flatpak Dependency Modules Presence**:
  - *Assertion*: Modules list includes `Imath`, `OpenEXR`, `OpenColorIO`, `OpenImageIO`, `PortAudio`, and `Olive`.

---

### 4.3 Detailed Test Specification: Tier 2 (Boundary & Corner Cases)
*Requirement: $\ge 5$ tests per feature for all 11 features ($55$ test cases).*

#### Feature 1: Parametric Equalizer Node (Boundaries)
- **T2.01.01 — Zero Gain (Unity Pass-Through)**: All bands gain set to 0.0 dB. Verify output samples equal input samples within numerical precision ($<10^{-6}$).
- **T2.01.02 — Extreme Gain Limits ($\pm 24$ dB)**: Configure $+24$ dB boost and $-24$ dB cut. Verify numerical stability, no `NaN` or `Inf` generated.
- **T2.01.03 — Nyquist Boundary Frequencies**: Set Low Pass at $f_0 = 23999$ Hz (at 48 kHz sample rate) and High Pass at $f_0 = 1$ Hz. Verify stable biquad coefficients ($a_0, a_1, a_2, b_0, b_1, b_2 \ne 0$).
- **T2.01.04 — Extreme Q Factors ($Q = 0.01$ and $Q = 100.0$)**: Ultra-wide and ultra-narrow resonance peaks. Verify filter does not oscillate into infinity or denormals.
- **T2.01.05 — Empty & Single-Sample Buffer**: Process `SampleBuffer` with sample count $0$ and sample count $1$. Verify no segmentation faults or out-of-bounds array access.

#### Feature 2: Track Audio Controls (Boundaries)
- **T2.02.01 — Minimum Volume Silence**: `kVolumeInput = 0.0` (-$\infty$ dB). Output buffer must contain pure $0.0f$ on all channels.
- **T2.02.02 — Maximum Volume Headroom**: `kVolumeInput = 4.0` (+12 dB). Verify buffer scales accurately without mathematical overflow.
- **T2.02.03 — Mute and Solo Conflict Resolution**: Track has both `kMutedInput = true` and `kSoloInput = true`. Verify design contract: explicit mute silences track even when soloed.
- **T2.02.04 — All Tracks Soloed**: In a 4-track sequence, set `kSoloInput = true` on all 4 tracks. Verify output is identical to having 0 tracks soloed (all active).
- **T2.02.05 — Rapid Keyframed Pan Modulation**: Pan oscillating between -1.0 and +1.0 across consecutive samples. Verify smooth interpolations without clicking or DC offset.

#### Feature 3: Track Audio Mixer Panel (Boundaries)
- **T2.03.01 — Sequence with 0 Audio Tracks**: Open sequence with only video tracks. Mixer panel displays master strip cleanly with no track strips and no crashes.
- **T2.03.02 — Sequence with 32+ Audio Tracks**: High track count stress test. Verify UI layout scrolls cleanly without widget overlap or memory explosion.
- **T2.03.03 — Rapid Track Addition/Removal**: Add 10 tracks and delete them immediately via UndoStack. Mixer panel strips update cleanly with no dangling pointers.
- **T2.03.04 — Track Renaming Propagation**: Rename track on timeline. Mixer panel strip label updates immediately.
- **T2.03.05 — Mixer Panel Destruction During Playback**: Close/hide mixer dock widget while audio playback thread is running. Verify zero thread synchronization crashes.

#### Feature 4: Thread-Safe VU Metering (Boundaries)
- **T2.04.01 — Sub-Normal / Negative Decibel Floor**: Feed signal at -120 dBFS. Meter registers clamp smoothly to floor ($-\infty$ or minimum dB mark) without numerical underflow.
- **T2.04.02 — DC Offset Signal ($+1.0$ constant)**: Feed flat DC line. Meter correctly displays peak $1.0$ and does not lock up ballistics filter.
- **T2.04.03 — Rapid Audio Engine Start/Stop Transitions**: Rapidly start and stop audio stream 50 times in 1 second. Atomic registers read safely with no race conditions.
- **T2.04.04 — Nan/Inf Sample Immunity**: Feed corrupt audio buffer containing `std::numeric_limits<float>::quiet_NaN()` and `infinity()`. VU meter calculation filters out invalid values and clamps to valid range.
- **T2.04.05 — Extended Decay Ballistics (10 seconds silence)**: Leave meter idle for 10 seconds. Verify level reaches absolute minimum and ballistics timer idles (saving CPU).

#### Feature 5: Asynchronous Scene Cut Analysis (Boundaries)
- **T2.05.01 — Video with Zero Cuts (Static Scene)**: 10-second single color video. Verify `SceneCutCompleted` returns empty cut list (`cuts.empty() == true`).
- **T2.05.02 — Single-Frame Video Clip**: 1-frame video file. Detector processes single frame gracefully and returns 0 cuts without crashing.
- **T2.05.03 — Alternating Black/White Frames (Strobing)**: 30 fps video alternating black/white every frame. Verify detector does not overflow memory or crash; minimum scene duration filter prevents micro-splits.
- **T2.05.04 — Corrupted / Truncated Video File**: Pass truncated MP4 container. `SceneCutTask` logs error and exits gracefully with `Finished(false)`, without aborting process.
- **T2.05.05 — Sensitivity Slider Extremes (Threshold 0.01 vs 0.99)**: Sensitivity at 0.01 triggers on subtle lighting changes; sensitivity at 0.99 triggers only on total contrast flips.

#### Feature 6: Timeline Auto-Split (Boundaries)
- **T2.06.01 — Split at Exact In-Point ($t = 0$)**: Cut timestamp identical to block `in()`. Command ignores point or avoids creating 0-length block.
- **T2.06.02 — Split at Exact Out-Point ($t = \text{length}$)**: Cut timestamp identical to block `out()`. Handled safely with no zero-length fragment.
- **T2.06.03 — Split Points Outside Block Range**: Cut timestamps at negative time and time $> \text{duration}$. Points safely ignored.
- **T2.06.04 — Unsorted & Duplicate Timestamps**: Pass $\{7.5, 2.1, 2.1, 5.0, 5.0\}$. Timestamps sorted and deduplicated internally before splitting.
- **T2.06.05 — Sub-Frame Rational Cut Timestamps**: Cut timestamp with fractional frames (e.g. $1001/30000$s). Verifies exact rational arithmetic without rounding truncation.

#### Feature 7: Final Cut Pro 7 XML Interchange (Boundaries)
- **T2.07.01 — Empty Sequence / Zero Clips**: Export and re-import sequence with tracks but 0 clips. Restores valid empty sequence structure.
- **T2.07.02 — Fractional NTSC Frame Rates**: 23.976 fps ($24000/1001$) and 29.97 fps ($30000/1001$). Timebase serialized as `<timebase>24</timebase>` with `<ntsc>TRUE</ntsc>`. Restores exact rational rate on import.
- **T2.07.03 — Special XML Characters in Marker Names**: Markers containing `<`, `>`, `&`, `"`, `'` and UTF-8 Japanese/Cyrillic characters. Re-imported without XML parsing failure.
- **T2.07.04 — Truncated / Malformed XML File**: Incomplete XML stream missing closing `</xmeml>` tag. `LoadFCPXMLTask` returns `kCorruptFile` error without crashing.
- **T2.07.05 — Missing Media Files on Relink**: FCP7 XML pointing to missing absolute media path. Olive loads sequence with offline placeholder clips instead of failing.

#### Feature 8: OpenTimelineIO Hardening (Boundaries)
- **T2.08.01 — Adjacent Zero-Length Transitions**: Clips abutting with 0-duration transition. Handled safely without null-pointer dereference.
- **T2.08.02 — Negative Speed / Reversed Clips**: Clip with `speed = -1.0` (Reverse playback). Saved to OTIO `LinearTimeWarp` with negative rate; loaded back with reverse flag preserved.
- **T2.08.03 — Large Marker Counts (1000+ markers)**: Timeline with 1000 markers serialized to OTIO. Memory usage remains linear, 0 leaks under ASan.
- **T2.08.04 — Malformed JSON Input**: Corrupt JSON passed to `LoadOTIOTask`. Gracefully returns error status.
- **T2.08.05 — Missing Schema Fields**: OTIO JSON from third-party tool missing optional metadata fields. Successfully parsed using defaults.

#### Feature 9: Main Menu Export Wiring (Boundaries)
- **T2.09.01 — Export Dialog Cancelation**: Open export dialog and immediately cancel. No file written, no tasks left lingering in `TaskManager`.
- **T2.09.02 — Export to Read-Only Directory**: Select destination without write permissions (`/proc/test.xml`). Clean error dialog displayed, no crash.
- **T2.09.03 — Export Overwrite Existing File**: Select existing file. Prompts user for confirmation; overwrites cleanly if confirmed.
- **T2.09.04 — Rapid Double-Triggering of Export Action**: Click export action twice in rapid succession. Only single dialog/task instantiated.
- **T2.09.05 — Menu Action with Corrupted Sequence**: Trigger export on sequence with missing node links. Gracefully displays error rather than crashing.

#### Feature 10: Linux AppImage Packaging (Boundaries)
- **T2.10.01 — Missing Dependency Detection in Build Script**: Execute `build_appimage.sh` with missing `libqxcb.so`. Script halts immediately with non-zero exit code and explicit error message.
- **T2.10.02 — Space in Installation Directory Path**: Staged installation path containing spaces (`/tmp/olive test app/`). `AppRun` handles paths safely with quoted variables.
- **T2.10.03 — Symlink Integrity in AppDir**: Verify that all symlinks in `AppDir/usr/lib` resolve to valid targets (no broken symlinks).
- **T2.10.04 — Stripping Debug Symbols Verification**: Ensure packaging script correctly strips unneeded debug symbols from bundled shared libraries.
- **T2.10.05 — Permissions Audit**: Ensure all binaries in `AppDir/usr/bin/` have `0755` executable permissions and data files have `0644`.

#### Feature 11: Linux Flatpak Packaging (Boundaries)
- **T2.11.01 — Malformed JSON Syntax Check**: Inject missing bracket into manifest. Validator catches error and reports line number.
- **T2.11.02 — Duplicate Module Names**: Check manifest modules for duplicate names.
- **T2.11.03 — Source Archive Hash Verification**: Verify that all file sources in Flatpak manifest specify SHA256 checksums.
- **T2.11.04 — Build System Type Integrity**: Verify every module specifies valid buildsystem (`cmake-ninja`, `autotools`, or `simple`).
- **T2.11.05 — Cleanup Rules Audit**: Verify cleanup list removes `.a` static libraries, headers, and cmake development files from final Flatpak bundle.

---

### 4.4 Detailed Test Specification: Tier 3 (Cross-Feature Combinations)
*Requirement: Pairwise interactions across features (15 comprehensive combinations).*

1. **T3.01 — Parametric EQ on Auto-Split Clips**:
   - *Interaction*: Feature 1 (Parametric EQ) $\times$ Feature 6 (Timeline Auto-Split).
   - *Scenario*: Apply Parametric EQ to a long audio clip. Trigger auto-split into 3 clips. Verify EQ node parameters are copied cleanly to all split clips and audio renders identically across split boundaries without clicks.
2. **T3.02 — Track Audio Controls on Auto-Split Sequence**:
   - *Interaction*: Feature 2 (Track Controls) $\times$ Feature 6 (Timeline Auto-Split).
   - *Scenario*: Adjust Track volume to -3 dB and pan to +0.5. Split clips at multiple points. Verify track-level controls remain intact and continue attenuating all split blocks uniformly.
3. **T3.03 — Multi-Track Mixer Summation & Thread-Safe VU Metering**:
   - *Interaction*: Feature 2/3 (Mixer) $\times$ Feature 4 (VU Metering).
   - *Scenario*: Play 4 tracks simultaneously with different pan/volume settings. Verify master VU meter accurately registers the summed acoustic power of all 4 tracks in real-time.
4. **T3.04 — Solo/Mute State Changes Reflected on VU Meters**:
   - *Interaction*: Feature 2 (Track Solo/Mute) $\times$ Feature 4 (VU Metering).
   - *Scenario*: Solo Track 1; verify Track 2 VU meter drops to $-\infty$ while Track 1 VU meter continues reading active levels.
5. **T3.05 — Asynchronous Scene Cut Detection Driving Timeline Auto-Split**:
   - *Interaction*: Feature 5 (Scene Cut Task) $\times$ Feature 6 (Timeline Auto-Split).
   - *Scenario*: Connect `SceneCutTask::SceneCutCompleted` directly to `BlockSplitPreservingLinksCommand`. Run detection on test video and auto-split selected clip and linked audio on timeline. Verify clip count equals detected cuts $+ 1$.
6. **T3.06 — Scene Cut Auto-Split with Full Undo/Redo**:
   - *Interaction*: Feature 5 (Scene Cut Task) $\times$ Feature 6 (Auto-Split Undo/Redo).
   - *Scenario*: Auto-split driven by scene cuts, followed immediately by `UndoStack::undo()`, then `redo()`. Verify timeline state consistency and 0 memory leaks under ASan.
7. **T3.07 — FCP7 XML Roundtrip of Scene-Cut Split Clips**:
   - *Interaction*: Feature 6 (Auto-Split) $\times$ Feature 7 (FCP7 XML).
   - *Scenario*: Auto-split a video clip into 5 shots. Export to FCP7 XML. Re-import into a fresh project. Verify all 5 shots appear with exact matching edit points.
8. **T3.08 — FCP7 XML Roundtrip with Multi-Band Audio & Volume/Pan Settings**:
   - *Interaction*: Feature 2 (Track Audio Controls) $\times$ Feature 7 (FCP7 XML).
   - *Scenario*: Sequence with multiple audio tracks having non-default volume and pan levels. Export to FCP7 XML and re-import. Verify track volumes/pan levels map accurately.
9. **T3.09 — OTIO Roundtrip of Auto-Split Video & Audio Linked Clips**:
   - *Interaction*: Feature 6 (Auto-Split) $\times$ Feature 8 (OTIO).
   - *Scenario*: Split linked video/audio clip. Export to OTIO. Re-import and verify video and audio tracks retain proper clip boundaries and synchrony.
10. **T3.10 — OTIO Roundtrip with Transitions on Auto-Split Edges**:
    - *Interaction*: Feature 6 (Auto-Split) $\times$ Feature 8 (OTIO Transitions).
    - *Scenario*: Auto-split clip at $t=5.0$, place Cross Dissolve transition at split cut point. Export to OTIO (verifying line 180 bugfix). Re-import and verify transition is fully reconstructed.
11. **T3.11 — Main Menu Triggering FCP7 XML Export of Auto-Split Project**:
    - *Interaction*: Feature 6 (Auto-Split) $\times$ Feature 9 (Main Menu Wiring) $\times$ Feature 7 (FCP7 XML).
    - *Scenario*: User creates edits via auto-split, triggers Export FCP7 XML from Main Menu. Verify export task executes successfully and generates valid file.
12. **T3.12 — Main Menu Triggering OTIO Export with Timeline Markers**:
    - *Interaction*: Feature 8 (OTIO Markers) $\times$ Feature 9 (Main Menu Wiring).
    - *Scenario*: Add markers to timeline, invoke OTIO export via Main Menu action. Verify markers are serialized into the generated file.
13. **T3.13 — AppImage Environment Running Parametric EQ & Mixer DSP**:
    - *Interaction*: Feature 1 (Equalizer) $\times$ Feature 10 (AppImage Packaging).
    - *Scenario*: Run E2E test binary inside staged AppImage environment (with `LD_LIBRARY_PATH` and `QT_PLUGIN_PATH`). Verify DSP audio processing executes without missing symbol errors.
14. **T3.14 — Flatpak Environment Running FFmpeg Scene Cut Decoding**:
    - *Interaction*: Feature 5 (Scene Cut) $\times$ Feature 11 (Flatpak Packaging).
    - *Scenario*: Verify Flatpak manifest includes required FFmpeg codecs and decoders necessary for `SceneCutTask` execution without missing codec errors.
15. **T3.15 — Simultaneous Background Scene Cut Analysis and Live Audio EQ Playback**:
    - *Interaction*: Feature 1 (Parametric EQ) $\times$ Feature 4 (VU Meters) $\times$ Feature 5 (Async Scene Cut Task).
    - *Scenario*: Start `SceneCutTask` on a background thread while simultaneously rendering audio buffers through `EqualizerNode` and updating VU meter atomics. Verify zero thread contention, zero glitches, and clean ASan execution.

---

### 4.5 Detailed Test Specification: Tier 4 (Real-World Application Scenarios)
*Requirement: Full multi-step workflows.*

#### Scenario 1: Complete Editorial Workflow (Ingest $\to$ Detect Cuts $\to$ Split $\to$ Mix $\to$ Export FCP7 XML)
- **ID**: `T4.01_CompleteEditorialFCPXML`
- **Workflow**:
  1. Create a new `Project` and `Sequence` (1080p24, 48 kHz stereo).
  2. Ingest raw 30-second camera file (synthetic video with 4 distinct scenes at $t=5, 12, 19, 25$).
  3. Run `SceneCutTask` in background thread pool. Await `SceneCutCompleted`.
  4. Automatically split timeline clip at all detected cut timestamps using `BlockSplitPreservingLinksCommand`.
  5. Add dialogue audio to Track A1 and background music to Track A2.
  6. Insert `EqualizerNode` on Dialogue track: Apply High Pass at 80 Hz (rumble filter), Peaking Bell at 3 kHz (+3 dB vocal presence), and High Shelf at 10 kHz (-2 dB de-esser).
  7. Set Dialogue volume to $0$ dB (center pan); set Music volume to $-12$ dB (slight pan).
  8. Add markers at key scene moments ("Intro", "Climax", "Outro").
  9. Add Cross Dissolve transition between Scene 2 and Scene 3.
  10. Export sequence to FCP7 XML via `SaveFCPXMLTask`.
  11. Load exported XML into a blank `Project` via `LoadFCPXMLTask`.
- **Pass Criteria**:
  - Re-imported sequence contains exactly 5 video blocks and 5 audio blocks.
  - In/out points match original cut timestamps within 0 frames.
  - Markers and Cross Dissolve transition are reconstructed accurately.
  - Zero memory leaks reported under AddressSanitizer.

#### Scenario 2: Modern NLE Collaborative Interchange (Olive $\leftrightarrow$ Premiere/Kdenlive via OTIO)
- **ID**: `T4.02_InterchangeOTIOCollaboration`
- **Workflow**:
  1. Ingest multi-track project exported from external NLE.
  2. Parse and load via `LoadOTIOTask`.
  3. Verify imported timeline has accurate video tracks, audio tracks, clip durations, and speed factors (including a 1.5x fast-motion clip).
  4. Perform editorial modifications in Olive: split a clip, adjust audio track volume and pan, and add review markers.
  5. Export modified timeline to `.otio` via `SaveOTIOTask`.
  6. Verify OTIO JSON syntax and validate with standard OTIO schema:
     - `LinearTimeWarp` preserved.
     - New split clips preserved.
     - Transitions have valid offsets (no memory leak or clobber).
     - Markers preserved.
- **Pass Criteria**:
  - Bi-directional fidelity with 0 frame shift and 0 schema corruption.

#### Scenario 3: Live Broadcast Fast-Turnaround Audio Mastering & Metering
- **ID**: `T4.03_LiveBroadcastAudioMastering`
- **Workflow**:
  1. Setup 8-track audio sequence simulating multi-mic broadcast (Host, Guest 1, Guest 2, Ambient, Music L/R, Sound FX 1/2).
  2. Configure parametric EQ on all dialogue channels (custom notch filters at 60 Hz hum, speech clarity boost).
  3. Simulate real-time audio playback through `Track::ProcessAudioTrack` and Master bus summation for 10 simulated seconds.
  4. Concurrently update and sample thread-safe VU meters across all 8 tracks and master bus.
  5. Toggle Solo on Host mic during speech segment; verify all other 7 channels mute instantly on master VU meter.
  6. Un-solo; verify smooth return of all channels with proper ballistics decay.
- **Pass Criteria**:
  - Zero audio dropouts or buffer under-runs in audio simulation.
  - Master VU meter reflects precise theoretical decibel sum.
  - TSan and ASan confirm zero thread races and zero memory leaks.

#### Scenario 4: Fast Archival & Batch Auto-Split Ingest
- **ID**: `T4.04_ArchivalBatchAutoSplit`
- **Workflow**:
  1. Batch enqueue 5 different video archives into `TaskManager` for asynchronous scene cut detection.
  2. Concurrently monitor progress of all 5 tasks via `ProgressChanged` signals.
  3. Cancel 1 task midway through processing.
  4. Allow the other 4 tasks to complete.
  5. Sequentially auto-split corresponding timeline tracks for each finished task.
  6. Execute `UndoStack::undo()` on two of the splits, and `redo()` on one.
- **Pass Criteria**:
  - Cancelled task terminates cleanly without leaking FFmpeg decoder memory.
  - Completed tasks produce verified cuts.
  - Undo/redo state is 100% deterministic and free of leaks.

#### Scenario 5: Full Headless Packaging & CLI Deployment Pipeline
- **ID**: `T4.05_PackagingAndHeadlessDeployment`
- **Workflow**:
  1. Lint Flatpak manifest `packaging/flatpak/org.olivevideoeditor.Olive.json` against KDE 6.8 runtime requirements.
  2. Lint AppImage build script `packaging/linux/build_appimage.sh` and `app/packaging/linux/AppRun`.
  3. Stage Olive binary installation into temporary prefix (`cmake --install build-linux-asan --prefix /tmp/olive-stage`).
  4. Execute installed binary in headless mode with `--version` and `--help`.
  5. Execute headless project decompression (`olive-editor -d <test.ove>`).
  6. Execute headless project export (`olive-editor -x <test.ove>`).
- **Pass Criteria**:
  - All scripts pass linting.
  - Headless CLI invocations succeed with exit code 0.
  - Packaging files satisfy all distribution prerequisites.

---

### 4.6 Implementation Blueprint for `tests/e2e/`

#### 1. `tests/CMakeLists.txt` Integration
Add the following line to `tests/CMakeLists.txt`:
```cmake
add_subdirectory(e2e)
```

#### 2. `tests/e2e/CMakeLists.txt` Specification
```cmake
# tests/e2e/CMakeLists.txt
olive_add_test(E2E e2e-audio-tests e2e_audio_tests.cpp)
olive_add_test(E2E e2e-scenecut-tests e2e_scenecut_tests.cpp)
olive_add_test(E2E e2e-interchange-tests e2e_interchange_tests.cpp)
olive_add_test(E2E e2e-workflow-tests e2e_workflow_tests.cpp)

find_program(OLIVE_TEST_FFMPEG ffmpeg)
if(OLIVE_TEST_FFMPEG)
  target_compile_definitions(e2e-scenecut-tests PRIVATE OLIVE_TEST_FFMPEG="${OLIVE_TEST_FFMPEG}")
  target_compile_definitions(e2e-workflow-tests PRIVATE OLIVE_TEST_FFMPEG="${OLIVE_TEST_FFMPEG}")
endif()

# Register Python E2E packaging and test suite in CTest
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
  NAME e2e-packaging-validation
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_packaging.py
)
set_tests_properties(e2e-packaging-validation PROPERTIES TIMEOUT 60 LABELS "E2E")
```

#### 3. Python Test Runner (`tests/e2e/run_e2e.py`) Specification
```python
#!/usr/bin/env python3
"""Olive Video Editor - End-to-End Test Suite Runner."""
import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def run_suite(preset="linux-asan", tier="all", verbose=False):
    build_dir = ROOT / f"build-{preset}"
    if not build_dir.exists():
        print(f"Error: build directory {build_dir} does not exist. Configure and build first.")
        return 1

    # 1. Run packaging validation tests
    print("[E2E] Running Packaging Validation...")
    pkg_res = subprocess.run([sys.executable, str(ROOT / "tests" / "e2e" / "test_packaging.py")])
    if pkg_res.returncode != 0:
        print("[E2E] Packaging validation failed!")
        return pkg_res.returncode

    # 2. Run CTest E2E tests under ASan
    label = "E2E"
    cmd = ["ctest", "--test-dir", str(build_dir), "-L", label, "--output-on-failure"]
    if verbose:
        cmd.append("-V")
    print(f"[E2E] Running CTest E2E executables ({' '.join(cmd)})...")
    ctest_res = subprocess.run(cmd)
    return ctest_res.returncode

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Olive E2E Test Suite Runner")
    parser.add_argument("--preset", default="linux-asan", help="CMake preset")
    parser.add_argument("--tier", default="all", choices=["1", "2", "3", "4", "all"])
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    sys.exit(run_suite(preset=args.preset, tier=args.tier, verbose=args.verbose))
```

---

## 5. Verification Method

To independently verify the facts, architecture, and specifications in this report:

1. **Verify Existing CTest Architecture**:
   ```bash
   ctest --test-dir /home/yuri/Documentos/olive/build-linux-asan --show-only=json-v1
   ```
   *Expected result*: Confirms that CTest catalogs all registered executables and executes them under AddressSanitizer.
2. **Verify Python Environment**:
   ```bash
   python3 -c "import unittest, subprocess, json; print('Python standard library OK')"
   ```
   *Expected result*: Outputs `Python standard library OK`.
3. **Verify FFmpeg Availability for Synthetic Media**:
   ```bash
   ffmpeg -version | head -n 1
   ```
   *Expected result*: Confirms presence of FFmpeg CLI for generating zero-bloat test clips.
4. **Verify Gauntlet Compliance Gate**:
   ```bash
   python3 /home/yuri/Documentos/olive/scripts/gauntlet.py --preset linux-asan --jobs 4
   ```
   *Expected result*: Gauntlet compiles the project, runs CTest targets, reports 100% pass, and produces report in `qa-results/`.
5. **Inspect Test Specification Artifact**:
   Inspect this file at:
   `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_e2e_1/handoff.md`
