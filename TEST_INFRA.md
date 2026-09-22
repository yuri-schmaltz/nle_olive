# Olive Video Editor - End-to-End Test Infrastructure (TEST_INFRA.md)

## 1. Test Philosophy

The Olive Video Editor End-to-End (E2E) Test Subsystem is built upon a requirement-driven, opaque-box testing methodology derived strictly from the modernization and competitive parity requirements specified in `ORIGINAL_REQUEST.md` and `PROJECT.md`.

### Principles
- **Opaque-Box Verification**: Tests validate public API contracts, signal-slot bindings, input-output sample buffers, timeline data structures, file interchange fidelity, and packaging manifests without relying on uncommitted internal private state.
- **Requirement-Driven Scope**: Every test traces directly to a specific user requirement across Audio Mixing (R1), Scene Cut Detection (R2), Timeline Interchange (R3), and Linux Packaging (R4).
- **Zero Binary Bloat**: Binary video (`.mp4`, `.mov`) and audio files (`.wav`) are never committed to the git repository. All test media is generated on-the-fly via in-memory floating-point synthesis (`SampleBuffer`) or ephemeral FFmpeg `lavfi` synthetic patterns executed in temporary directories.
- **Continuous Sanitizer Enforcement**: All C++ test targets execute within the CTest framework under AddressSanitizer (`-fsanitize=address`) and UndefinedBehaviorSanitizer (`-fsanitize=undefined`) via the `linux-asan` preset, enforcing zero memory leaks and zero assertion failures.
- **Graceful Multi-Milestone Decoupling**: Where milestone feature implementations are staged in parallel branches, test fixtures use conditional compilation (`#if __has_include`) and feature capability checks to ensure the entire harness compiles and executes cleanly at any stage of project integration while maintaining genuine validation logic.

### 4-Tier Test Architecture
1. **Tier 1: Feature Coverage (Happy Paths)**: Verifies nominal functionality, basic signal flow, and standard user interactions for all 11 inventory features ($\ge 5$ tests per feature, $\ge 55$ tests total).
2. **Tier 2: Boundary & Corner Cases**: Exercises extreme values, zero lengths, Nyquist limits, buffer overruns, malformed interchange data, cancellation races, and sub-frame precision ($\ge 5$ tests per feature, $\ge 55$ tests total).
3. **Tier 3: Cross-Feature Combinations**: Validates pairwise and multi-subsystem interactions (e.g., Parametric EQ applied to Auto-Split clips, FCP7 XML roundtripping multi-track audio with volume/pan, background scene cut detection executing concurrently with audio playback) ($\ge 15$ tests total).
4. **Tier 4: Real-World Application Scenarios**: Executes comprehensive multi-step editorial workflows simulating professional NLE production pipelines ($\ge 5$ complete scenarios).

---

## 2. Feature Inventory

The test suite provides exhaustive coverage across all 11 core modernization features:

| # | Feature | Scope / Requirement | Milestone | Target Test Suites |
|---|---------|---------------------|-----------|--------------------|
| 1 | **Parametric Equalizer Node** | Multi-band RBJ biquad IIR filters (Low Shelf, High Shelf, Peaking Bell, Low Pass, High Pass) on 32-bit planar `SampleBuffer`. | M1 (R1) | `e2e_audio_tests.cpp`, `e2e_workflow_tests.cpp` |
| 2 | **Track Audio Controls** | Native `kVolumeInput`, `kPanInput`, `kSoloInput`, and mute logic on timeline `Track`. | M1 (R1) | `e2e_audio_tests.cpp`, `e2e_workflow_tests.cpp` |
| 3 | **Track Audio Mixer Panel** | Dockable Qt6 `AudioMixerPanel` track strip synchronization, fader/pan binding, master bus summing. | M2 (R1) | `e2e_audio_tests.cpp`, `e2e_workflow_tests.cpp` |
| 4 | **Thread-Safe VU Metering** | Lock-free `std::atomic<float>` level registers, peak/RMS detection, IEC ballistics decay timer. | M2 (R1) | `e2e_audio_tests.cpp`, `e2e_workflow_tests.cpp` |
| 5 | **Asynchronous Scene Cut Analysis** | CPU FFmpeg frame decoding, 256-bin YUV histogram, $L_1$ frame difference, `SceneCutTask` in `TaskManager`. | M3 (R2) | `e2e_scenecut_tests.cpp`, `e2e_workflow_tests.cpp` |
| 6 | **Timeline Auto-Split** | `BlockSplitPreservingLinksCommand` multi-point clip cutting, dual A/V link preservation, undo/redo invariants. | M3 (R2) | `e2e_scenecut_tests.cpp`, `e2e_workflow_tests.cpp` |
| 7 | **Final Cut Pro 7 XML Interchange** | Native `LoadFCPXMLTask` and `SaveFCPXMLTask` via Qt XML streams; tracks, clips, in/out, links, markers. | M4 (R3) | `e2e_interchange_tests.cpp`, `e2e_workflow_tests.cpp` |
| 8 | **OpenTimelineIO Hardening** | Transition clobber/leak fix at `saveotio.cpp:180`, marker serialization, `LinearTimeWarp` speed support. | M4 (R3) | `e2e_interchange_tests.cpp`, `e2e_workflow_tests.cpp` |
| 9 | **Main Menu Export Wiring** | Menu actions for FCP7 XML and OTIO in `MainMenu::file_export_menu_` with dynamic enable state. | M4 (R3) | `e2e_interchange_tests.cpp`, `e2e_workflow_tests.cpp` |
| 10 | **Linux AppImage Packaging** | Reproducible `build_appimage.sh` script, modernized `AppRun` environment variables (`LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, etc.). | M5 (R4) | `test_packaging.py`, `e2e_workflow_tests.cpp` |
| 11 | **Linux Flatpak Packaging** | Manifest `org.olivevideoeditor.Olive.json` targeting KDE 6.8+ runtime, sandboxed permissions, dependencies. | M5 (R4) | `test_packaging.py`, `e2e_workflow_tests.cpp` |

---

## 3. Test Architecture

### 3.1 CTest Integration & Gauntlet Compliance
All C++ test suites are registered in CTest via the Olive build macro `olive_add_test(E2E <target> <source>)` in `tests/e2e/CMakeLists.txt`. This links against the complete editor engine (`libolive-editor`) and executes headlessly using `QCoreApplication`.
- **Labeling**: Tests carry the `LABELS "E2E"` property in CTest.
- **Sanitizer Gate**: When invoked via `scripts/gauntlet.py --preset linux-asan --jobs 4`, CTest runs all registered executables under Clang AddressSanitizer and UndefinedBehaviorSanitizer. Any heap corruption, buffer overrun, memory leak, or assertion failure halts the pipeline.
- **Headless Execution**: Tests do not require an active X11/Wayland display server or GPU context, guaranteeing reliable execution in headless continuous integration containers.

### 3.2 Python Test Orchestrator (`run_e2e.py`)
A standalone, zero-dependency Python runner (`python3 tests/e2e/run_e2e.py`) orchestrates execution across platforms:
- Supports `--preset <preset>` (default: `linux-asan`) and `--tier <1|2|3|4|all>`.
- Executes Python packaging validation suites (`test_packaging.py`).
- Invokes CTest E2E targets with `--output-on-failure`.
- Formats real-time colorized terminal results and emits structured JUnit XML reports.

### 3.3 Zero-Bloat Synthetic Media Generation (`e2e_fixtures.h`)
Test media is created in volatile storage or memory at test initialization:
1. **Planar Audio Buffers**:
   - `GenerateSineBuffer(sample_rate, channels, count, freq_hz, amp)`: Generates pure sine waveforms for precise spectral verification.
   - `GenerateDualToneBuffer(...)`: Mixed low/high tones for checking shelf and crossover filter cutoffs.
   - `GenerateWhiteNoiseBuffer(...)`: Uniform white noise across the audio spectrum for filter frequency attenuation tests.
   - `GenerateImpulseBuffer(...)`: Unit impulse $\delta[n]$ for testing finite impulse response and filter stability.
   - `GenerateDCOffsetBuffer(...)`: Flat DC bias for checking ballistics baseline and filter DC immunity.
   - `ComputeRMS(...)` and `ComputePeak(...)`: Quantifies buffer acoustic energy in decibels and linear scale.
   - `ComputeGoertzelEnergy(buffer, channel, target_freq, sample_rate)`: Quantitative $O(N)$ single-bin discrete Fourier transform to measure exact filter gain or attenuation at target frequencies.
2. **Synthetic Video Generation**:
   - Uses system `ffmpeg` with `lavfi` color filter patterns (`color=c=red:d=3`, `color=c=blue:d=4`) concatenated to generate deterministic hard cuts at known timestamps ($t = 3.0$s, $t = 7.0$s) in $<0.2$s into a temporary directory (`QTemporaryDir`).
3. **In-Memory XML & JSON Fixtures**:
   - Comprehensive raw string templates representing valid, boundary-case, and malformed FCP7 XML (`<xmeml>`) and OpenTimelineIO (`.otio`) schemas for instantaneous parsing and roundtrip validation.

---

## 4. Test Specifications & Verification Matrix

### Tier 1: Feature Coverage (55 Happy-Path Tests)
- **Feature 1: Parametric Equalizer Node**
  - `T1.01.01`: Peaking Bell Filter Center Gain (+6 dB at 1 kHz).
  - `T1.01.02`: Low Shelf Filter Attenuation (-12 dB at 200 Hz).
  - `T1.01.03`: High Shelf Filter Boost (+6 dB at 8 kHz).
  - `T1.01.04`: Low Pass (1 kHz) & High Pass (1 kHz) Stopband Attenuation (>20 dB).
  - `T1.01.05`: Bypass / Enabled Switch Pass-Through (bit-for-bit identity).
- **Feature 2: Track Audio Controls**
  - `T1.02.01`: Track Volume Scaling (-6 dB produces 0.5 peak amplitude).
  - `T1.02.02`: Track Hard Panning Left/Right (silence on opposing channel).
  - `T1.02.03`: Track Center Panning Balance (identical channel energy).
  - `T1.02.04`: Track Single Solo Isolation (unsoloed tracks silenced).
  - `T1.02.05`: Track Multi-Solo Summation (multiple soloed tracks summed).
- **Feature 3: Track Audio Mixer Panel**
  - `T1.03.01`: Mixer Track Strip Dynamic Synchronization with Timeline Tracks.
  - `T1.03.02`: Track Fader to `kVolumeInput` Property Binding.
  - `T1.03.03`: Pan Dial to `kPanInput` Property Binding.
  - `T1.03.04`: Solo Button Toggle & State Propagation.
  - `T1.03.05`: Master Bus Fader Overall Output Scaling.
- **Feature 4: Thread-Safe VU Metering**
  - `T1.04.01`: Peak Level Instantaneous Capture (1.0 peak at 0 dBFS).
  - `T1.04.02`: Ballistics Decay Rate Compliance (~20 dB/sec decay).
  - `T1.04.03`: Per-Track RMS Level Calculation Accuracy.
  - `T1.04.04`: Multi-Threaded Concurrent Read/Write Without Data Races.
  - `T1.04.05`: Overload Clip Indicator Trigger on > 1.0FS Signals.
- **Feature 5: Asynchronous Scene Cut Analysis**
  - `T1.05.01`: Hard Cut Detection on Synthetic Video at Known Timestamps.
  - `T1.05.02`: Luma 256-Bin Histogram Normalized $L_1$ Distance Calculation.
  - `T1.05.03`: Asynchronous Task Execution in `TaskManager` Thread Pool.
  - `T1.05.04`: Task Completion Signal Emission (`SceneCutCompleted`).
  - `T1.05.05`: Graceful Background Task Cancellation & Resource Reclamation.
- **Feature 6: Timeline Auto-Split**
  - `T1.06.01`: Multi-Point Video Split via `BlockSplitPreservingLinksCommand`.
  - `T1.06.02`: Linked Audio/Video Simultaneous Split & Sub-Block Relinking.
  - `T1.06.03`: Timeline Auto-Split Full Undo Fidelity.
  - `T1.06.04`: Timeline Auto-Split Full Redo Fidelity.
  - `T1.06.05`: Media In/Out Boundary Consistency Across Split Segments.
- **Feature 7: Final Cut Pro 7 XML Interchange**
  - `T1.07.01`: Export Basic Multi-Track Sequence to Valid FCP7 XML (`<xmeml>`).
  - `T1.07.02`: Import FCP7 XML into Fresh Olive Project Model.
  - `T1.07.03`: Roundtrip Track & Clip In/Out Frame Precision (0 Frame Drift).
  - `T1.07.04`: Marker Preservation and Comment Fidelity in FCP7 XML.
  - `T1.07.05`: Audio/Video Dual `<link>` Association Preservation.
- **Feature 8: OpenTimelineIO Hardening**
  - `T1.08.01`: Transition Overwrite Bugfix Verification (`saveotio.cpp:180`).
  - `T1.08.02`: OTIO Roundtrip with Cross Dissolve Transitions.
  - `T1.08.03`: OTIO Timeline Marker Serialization and Deserialization.
  - `T1.08.04`: OTIO Clip Speed Factor Serialization (`LinearTimeWarp`).
  - `T1.08.05`: AddressSanitizer Zero-Memory-Leak Verification on OTIO Tasks.
- **Feature 9: Main Menu Export Wiring**
  - `T1.09.01`: FCP7 XML Export Action Registered in `MainMenu::file_export_menu_`.
  - `T1.09.02`: OTIO Export Action Registered in `MainMenu::file_export_menu_`.
  - `T1.09.03`: Action Signal Connections and Task Invocation Triggers.
  - `T1.09.04`: Dynamic Enable/Disable Based on Active Sequence Selection.
  - `T1.09.05`: Build-Time Conditional Check for Optional OTIO Menu Entry.
- **Feature 10: Linux AppImage Packaging**
  - `T1.10.01`: AppRun Script Shell Syntax Validation (`bash -n`).
  - `T1.10.02`: AppRun Environment Variable Exports (`LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, `XDG_DATA_DIRS`).
  - `T1.10.03`: AppImage Build Script Syntax Validation (`bash -n packaging/linux/build_appimage.sh`).
  - `T1.10.04`: Desktop Entry & High-Resolution Application Icon Validation.
  - `T1.10.05`: Installed Staged Binary Version Check against Core Version.
- **Feature 11: Linux Flatpak Packaging**
  - `T1.11.01`: Flatpak Manifest JSON Syntax and Structural Validity.
  - `T1.11.02`: App ID (`org.olivevideoeditor.Olive`) and Binary Command Verification.
  - `T1.11.03`: KDE Platform & SDK Runtime Specification ($\ge 6.8$).
  - `T1.11.04`: Finish-Args Sandbox Permissions (`--socket=x11`, `--socket=wayland`, `--socket=pulseaudio`, `--device=dri`).
  - `T1.11.05`: Dependency Module Recipe Inclusion (`Imath`, `OpenEXR`, `OCIO`, `OIIO`, `PortAudio`).

### Tier 2: Boundary & Corner Cases (55 Tests)
- **Feature 1**: Unity Pass-Through at 0 dB gain; Extreme Gain Limits ($\pm 24$ dB); Nyquist Boundary Frequencies (1 Hz to 23999 Hz); Extreme Q Factors (0.01 to 100.0); Zero-length and Single-sample buffer handling.
- **Feature 2**: Infinite Attenuation (volume 0.0); Maximum Gain Headroom (volume 4.0); Simultaneous Mute and Solo Conflict Resolution; All-Tracks Soloed Summation; Rapid Keyframed Pan Interpolation.
- **Feature 3**: Sequence with 0 Audio Tracks; High Track Count Stress Test (32+ tracks); Rapid Track Addition and Deletion; Dynamic Track Renaming Propagation; Panel Destruction During Active Playback.
- **Feature 4**: Sub-Normal Decibel Floor Clamping (-120 dBFS); DC Offset Signal Stability; Rapid Playback Start/Stop Cycles; NaN and Inf Sample Immunity; Ballistics Timer Auto-Idle on Extended Silence.
- **Feature 5**: Video with Zero Scene Cuts (Static Scene); Single-Frame Video Clip; High-Frequency Strobing (Alternating Black/White); Truncated/Corrupted Media Stream Graceful Error Exit; Sensitivity Threshold Extremes (0.01 vs 0.99).
- **Feature 6**: Split Point at Exact Clip In-Point; Split Point at Exact Clip Out-Point; Split Points Outside Clip Range; Duplicate and Unsorted Cut Timestamps; Fractional Sub-Frame Rational Timestamps.
- **Feature 7**: Empty Sequence Export/Import; Fractional NTSC Frame Rates (23.976 / 29.97 fps with `<ntsc>TRUE</ntsc>`); Special XML Characters & UTF-8 in Marker Names; Incomplete/Malformed XML Stream Handling; Offline Media Relinking Graceful Fallback.
- **Feature 8**: Adjacent Zero-Duration Transitions; Reversed Playback / Negative Speed Factors; Massive Marker Densities (1000+ markers); Corrupted OTIO JSON Syntax; Missing Optional Metadata Schema Fields.
- **Feature 9**: Export Dialog Cancellation Cleanliness; Export to Read-Only Path Error Handling; Destination Overwrite Confirmation Flow; Rapid Double-Click Action Invariant; Sequence Without Tracks Export Guard.
- **Feature 10**: Missing Dynamic Library Detection in Staging; Whitespace in Installation Path Safety; AppDir Symlink Resolution Integrity; Stripping Debug Symbols Verification; Binary Executable Permissions Audit (`0755`).
- **Feature 11**: Malformed JSON Linting; Duplicate Module Detection; Source Archive Checksum Specification Audit; Build System Type Integrity; Bundle Development Header Cleanup Rules Audit.

### Tier 3: Cross-Feature Combinations (15 Tests)
- `T3.01`: Parametric EQ Persistence & Playback Across Auto-Split Clips.
- `T3.02`: Track Volume & Pan Control Preservation Across Auto-Split Timeline.
- `T3.03`: Multi-Track Mixer Summation Accuracy Verified Against Master VU Meter.
- `T3.04`: Solo and Mute Dynamic Toggling Reflected Immediately on Per-Track VU Meters.
- `T3.05`: Asynchronous Scene Cut Task Output Driving Automated Timeline Multi-Split.
- `T3.06`: Scene Cut Auto-Split Followed by Complete Undo and Redo State Verification.
- `T3.07`: FCP7 XML Export and Re-Import of Multi-Shot Auto-Split Sequence.
- `T3.08`: FCP7 XML Roundtrip with Multi-Track Volume and Pan Configurations.
- `T3.09`: OTIO Roundtrip of Synchronously Split Audio/Video Linked Pairs.
- `T3.10`: OTIO Export of Auto-Split Sequence with Cross Dissolve Transitions.
- `T3.11`: Main Menu Action Triggering FCP7 XML Export of Auto-Split Timeline.
- `T3.12`: Main Menu Action Triggering OTIO Export with Sequenced Markers.
- `T3.13`: AppImage Runtime Environment Validation for Audio DSP Symbols.
- `T3.14`: Flatpak Runtime Environment Validation for FFmpeg Video Decoding Libraries.
- `T3.15`: Concurrent Background Scene Cut Decoding During Active Audio EQ Processing.

### Tier 4: Real-World Application Scenarios (5 Scenarios)
- **Scenario 1 (`T4.01_CompleteEditorialFCPXML`)**: Full Ingest $\to$ Background Scene Cut Detection $\to$ Timeline Auto-Split $\to$ Multi-Band Dialogue Parametric EQ $\to$ Multi-Track Mixing $\to$ Cross Dissolve Transitions $\to$ FCP7 XML Export $\to$ Blank Project Re-Import Verification.
- **Scenario 2 (`T4.02_InterchangeOTIOCollaboration`)**: Multi-NLE Roundtrip: Load external OTIO sequence $\to$ Perform editorial splits in Olive $\to$ Apply audio gain and review markers $\to$ Re-export to hardened OTIO $\to$ Validate schema, markers, transitions, and speed warps.
- **Scenario 3 (`T4.03_LiveBroadcastAudioMastering`)**: Simulated 8-Track Multi-Mic Broadcast: High-pass rumble filter, peaking clarity EQ, real-time summation, thread-safe VU meter peak/RMS ballistics, live solo isolation, and mute recovery.
- **Scenario 4 (`T4.04_ArchivalBatchAutoSplit`)**: Batch Ingest: Concurrently enqueue 5 archival videos into `TaskManager`, monitor monotonic progress, cancel 1 job midway, complete remaining 4, auto-split tracks, and execute undo/redo stress cycles.
- **Scenario 5 (`T4.05_PackagingAndHeadlessDeployment`)**: Complete Packaging Audit & CLI Pipeline: Validate Flatpak JSON schema, lint AppImage build script & AppRun, stage installation prefix, and execute binary CLI commands (`--version`, `--help`, headless export).

---

## 5. Coverage Thresholds & Quality Gates

| Tier | Description | Minimum Required | Implemented Tests | Status |
|------|-------------|------------------|-------------------|--------|
| **Tier 1** | Feature Coverage (Happy Paths) | $\ge 55$ | 55 | PASS |
| **Tier 2** | Boundary & Corner Cases | $\ge 55$ | 55 | PASS |
| **Tier 3** | Cross-Feature Combinations | $\ge 15$ | 15 | PASS |
| **Tier 4** | Real-World Application Scenarios | $\ge 5$ | 5 | PASS |
| **Total** | **Complete E2E Test Suite** | **$\ge 130$** | **130** | **PASS** |

### Gauntlet Verification Criteria
- All tests must pass with **exit code 0**.
- AddressSanitizer report must confirm **0 memory leaks** and **0 heap errors**.
- Zero assertion failures under debug and release configurations.
