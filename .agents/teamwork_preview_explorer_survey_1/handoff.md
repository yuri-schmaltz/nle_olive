# Handoff Report: Audio Subsystem & Professional Mixer Survey (R1)
**Agent**: Codebase Audio Explorer (`teamwork_preview_explorer_survey_1`)  
**Date**: September 20, 2026  
**Type**: Hard Handoff (Investigation Complete)

---

## 1. Observation

1. **Audio Buffer Architecture**:
   - `ext/core/include/olive/core/render/samplebuffer.h`: `olive::core::SampleBuffer` stores planar 32-bit floating point samples (`std::vector<std::vector<float>> data_`). Channel data is accessed via `float* data(int channel)` and `int channel_count() const`.
   - `ext/core/include/olive/core/render/audioparams.h`: Defines sample rate, channel layout (`uint64_t`, using FFmpeg constants such as `AV_CH_LAYOUT_STEREO`), and `SampleFormat` (defaulting to `SampleFormat::F32P`).

2. **Existing Audio Processing Nodes**:
   - `app/node/audio/volume/volume.h` & `volume.cpp`: `VolumeNode` inherits `MathNodeBase`. Inputs: `kSamplesInput` (`samples_in`), `kVolumeInput` (`volume_in`, float, dB view). Static inputs are processed directly in `Value()` via `buffer.transform_volume(volume)`. Non-static inputs generate a `SampleJob`.
   - `app/node/audio/pan/pan.h` & `pan.cpp`: `PanNode` inherits `Node`. Inputs: `kSamplesInput`, `kPanningInput` (`panning_in`, float, -1.0 to 1.0). Static inputs scale channel 0 or channel 1 directly.
   - `app/node/factory.h` & `factory.cpp`: `NodeFactory::InternalID` enumerates `kAudioVolume` and `kAudioPanning`. There are no other audio effect nodes registered in Olive.
   - `app/audio/audioprocessor.h` & `audioprocessor.cpp`: Uses FFmpeg `libavfilter` for `atempo` (pitch-preserving speed) and format conversion. There is **no biquad, IIR, FIR, or equalizer DSP code** in the codebase.

3. **Track Audio Architecture & Mixing**:
   - `app/node/output/track/track.h` & `track.cpp`: `Track : public Node` handles block aggregation for a track in `ProcessAudioTrack(...)`.
   - `Track` has `kBlockInput` and `kMutedInput` (`muted_in`). In `Track::GetActiveElementsAtTime` (`track.cpp:105`):
     ```cpp
     if (IsMuted() || blocks_.empty() || r.in() >= track_length() || r.out() <= 0) {
       return ActiveElements::kNoElements;
     }
     ```
   - `Track` has **no volume, pan, or solo inputs**.
   - `app/widget/timelinewidget/trackview/trackviewitem.cpp:67`: `solo_button_` was commented out (`/*solo_button_ = CreateMSLButton(tr("S"), Qt::yellow); layout->addWidget(solo_button_);*/`).
   - `app/timeline/timelineundogeneral.cpp:107`: Audio tracks in a `Sequence` are combined using `MathNode` with `kOpAdd` (`param_a_in` + `param_b_in`).

4. **UI Docking & Panel Architecture**:
   - `app/panel/panel.h`: `PanelWidget : public KDDockWidgets::DockWidget`. Panels are managed by `PanelManager` (`app/panel/panelmanager.h`) and automatically exposed in the main window's `Window` menu (`app/window/mainwindow/mainmenu.cpp:410`).
   - `app/window/mainwindow/mainwindow.cpp:846`: Panels are docked in `MainWindow::SetDefaultLayout()` using `addDockWidget` (e.g., `audio_monitor_panel_` docked right of timeline).

5. **Audio Metering & Playback Threading**:
   - `app/audio/audiomanager.cpp:58`: PortAudio stream uses `OutputCallback` reading from `PreviewAudioDevice` (`app/render/previewaudiodevice.cpp`), which synchronizes reads and writes using `QMutex lock_`.
   - `app/widget/audiomonitor/audiomonitor.cpp:422`: Existing `AudioMonitor` reads offline approximations from `AudioWaveformCache::GetSummaryFromTime` based on system clock delta rather than live playback streams.

6. **Unit Testing**:
   - `tests/CMakeLists.txt`: Defines `olive_add_test(GROUP NAME SOURCE)`. Test files use `tests/testutil.h` macros (`OLIVE_ADD_TEST`, `OLIVE_ASSERT`, `OLIVE_ASSERT_EQUAL`, `OLIVE_TEST_END`).
   - `tests/timeline/tempo-tests.cpp`: Reference audio test generating sine wave samples into `SampleBuffer`, feeding through track processing, and asserting output values.
   - Command `ctest --test-dir build-linux-asan -R TempoStream --output-on-failure` passes in 0.77s.

---

## 2. Logic Chain

1. **Audio Node Data Flow** (from Observation 1 & 2):
   - `SampleBuffer` is planar float (`std::vector<float>` per channel).
   - In `Node::Value()`, if inputs are static, audio processing can be executed directly in-place across the `SampleBuffer`.
   - If inputs have keyframes, a `SampleJob` is queued and processed sample-by-sample in `RenderProcessor::ProcessSamples`.
   - Therefore, a Parametric Equalizer node (`EqualizerNode`) can easily operate on `SampleBuffer` by calculating biquad coefficients and applying Direct Form II difference equations.

2. **Parametric Equalizer Implementation** (from Observation 2):
   - Because no native DSP code exists in Olive and `AudioProcessor` only wraps libavfilter for `atempo`, the EQ must be implemented as a clean, self-contained C++17 biquad filter module based on the Robert Bristow-Johnson Audio EQ Cookbook.
   - Registering `EqualizerNode` in `app/node/audio/equalizer/` with `NodeFactory::InternalID::kAudioEqualizer` integrates it into the Olive node library, effects menu, and property inspector.

3. **Track Audio Controls & Mixer UI** (from Observation 3 & 4):
   - Currently, `Track` lacks volume and pan controls, and multiple tracks are simply summed via `MathNode`.
   - Adding `kVolumeInput`, `kPanInput`, and `kSoloInput` directly to `Track` allows in-place volume and pan adjustments inside `Track::ProcessAudioTrack` and project file serialization without graph clutter.
   - A dedicated `AudioMixerPanel` inheriting `PanelWidget` can be tabbed with `AudioMonitorPanel` or docked beside the timeline, presenting channel strips for each audio track with faders, pan knobs, M/S buttons, and VU meters.

4. **Thread-Safe VU Metering** (from Observation 5):
   - In `OutputCallback` (or `PreviewAudioDevice::readData`), real-time playback levels can be computed and stored in `std::atomic<float>` peak/RMS registers with zero mutex contention and zero audio thread latency.
   - For individual tracks, levels can be queried from each track's `waveform_cache()` at the current playback playhead, matching how `AudioMonitor` operates.
   - The Qt GUI thread reads atomic levels using a 30-60 Hz `QTimer` with smooth ballistics (decay 20 dB/s).

5. **Test Strategy** (from Observation 6):
   - New audio tests can be added using `olive_add_test(Audio ...)` or integrated into `tests/timeline/` / `tests/audio/`.
   - Tests will synthesize sine and impulse buffers, pass them through `EqualizerNode` and `Track`, and assert frequency response, pan balance, and solo muting.

---

## 3. Caveats

- **External Plugins**: Olive currently does not support VST3, AU, or LADSPA audio plugins. Requirement R1 focuses strictly on native built-in mixing and parametric equalization.
- **Audio Output Device Selection**: Output stream format negotiation is handled by `AudioManager` and PortAudio; any multi-channel layout beyond stereo downmixes according to libavutil channel layout.

---

## 4. Conclusion

The Olive audio subsystem is well-structured for Requirement R1:
1. `SampleBuffer` provides direct planar float buffer access, ideal for high-performance DSP.
2. An RBJ biquad parametric equalizer (`EqualizerNode`) can be cleanly implemented in native C++17 with 5-6 bands (low shelf, peaking bell bands, high shelf, pass filters).
3. Track volume, pan, and solo should be added directly as inputs to `Track`, evaluated in `Track::ProcessAudioTrack`.
4. A new `AudioMixerPanel` (`KDDockWidgets::DockWidget`) should be introduced into `app/panel/` and `MainWindow`.
5. Real-time master and track VU metering can be computed lock-free using `std::atomic<float>` and `AudioWaveformCache`.
6. Full test coverage can be seamlessly hooked into `ctest` via `olive_add_test` in `tests/`.

---

## 5. Verification Method

To independently verify the facts and findings of this survey:

1. **Verify CTest and existing audio tests**:
   ```bash
   ctest --test-dir /home/yuri/Documentos/olive/build-linux-asan -R TempoStream --output-on-failure
   ```
2. **Inspect Existing Audio Nodes & Core Buffers**:
   - Inspect `ext/core/include/olive/core/render/samplebuffer.h`
   - Inspect `app/node/audio/volume/volume.cpp`
   - Inspect `app/node/audio/pan/pan.cpp`
3. **Inspect Track Audio Handling**:
   - Inspect `app/node/output/track/track.cpp` (lines 628-726 for `ProcessAudioTrack`)
   - Inspect `app/timeline/timelineundogeneral.cpp` (lines 107-112 for `MathNode` track merge)
4. **Inspect Panel and UI Setup**:
   - Inspect `app/window/mainwindow/mainwindow.cpp` (lines 823-874 for `SetDefaultLayout`)
5. **Full Survey Document**:
   - Detailed blueprint with equations, data flow diagrams, and class specifications is documented at:
     `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_1/survey_audio.md`
