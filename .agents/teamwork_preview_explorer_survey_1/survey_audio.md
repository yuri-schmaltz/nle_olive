# Comprehensive Codebase Survey: Audio Subsystem & Professional Audio Mixer (R1)
**Author**: Codebase Audio Explorer (`teamwork_preview_explorer_survey_1`)  
**Target Project**: Olive Video Editor (C++17 / Qt6)  
**Date**: September 20, 2026  
**Scope**: Requirement R1 — Professional Audio Module, Track Audio Mixer UI, Parametric Equalizer, VU Metering, Thread Safety, Unit Testing

---

## Executive Summary

Olive's audio subsystem is architected around a flexible Directed Acyclic Graph (DAG) node engine. Audio is internally passed as **planar 32-bit floating point buffers** (`olive::core::SampleBuffer`), where each channel occupies an isolated contiguous `std::vector<float>`. Audio evaluation operates ahead-of-time via background worker threads managed by `RenderManager` and cached in `AudioPlaybackCache` and `AudioWaveformCache`. Real-time playback to the hardware is driven by PortAudio via `AudioManager` and `PreviewAudioDevice`.

Currently, audio track mixing in Olive is rudimentary:
- Audio tracks (`Track`) are merged using generic mathematical addition nodes (`MathNode`).
- `Track` only supports Mute (`kMutedInput`) and Lock; track-level **Volume faders**, **Pan knobs**, and **Solo** controls are completely absent.
- The only audio effects currently implemented in `app/node/audio/` are `VolumeNode` and `PanNode`. There is **no digital signal processing (DSP) or audio filter code** (such as biquad/IIR filters or parametric equalizers) anywhere in the repository. The only external audio library used is FFmpeg's `libavfilter` (in `AudioProcessor`) exclusively for speed/tempo changes (`atempo`) and sample format conversion.
- Visual audio monitoring (`AudioMonitor`) reads offline approximations from `AudioWaveformCache` rather than real-time playback audio streams.

This survey establishes the complete blueprint for implementing:
1. Native track-level volume, pan, and solo controls integrated directly into `Track` and `Sequence`.
2. A professional dockable `AudioMixerPanel` (KDDockWidgets) with vertical dB faders, pan sliders, mute/solo buttons, and stereo peak/RMS VU meters.
3. A modular `EqualizerNode` in the audio node library implementing a multi-band cascaded Robert Bristow-Johnson biquad filter engine.
4. A thread-safe, lock-free metering pipeline bridging the PortAudio audio thread, worker threads, and Qt GUI thread.
5. Unit test suites using Olive's custom CTest harness (`tests/testutil.h`).

---

## 1. Audio Subsystem & DAG Node Architecture

### 1.1 Core Audio Data Structures

Audio in Olive is anchored in `ext/core/` and `app/audio/`:

#### `olive::core::SampleBuffer`
- **Definition**: `ext/core/include/olive/core/render/samplebuffer.h`
- **Implementation**: `ext/core/src/render/samplebuffer.cpp`
- **Storage Layout**: Planar 32-bit float (`std::vector<std::vector<float>> data_`).
  - Channel 0: `float* data(0)` with `sample_count()` samples.
  - Channel 1: `float* data(1)` with `sample_count()` samples.
- **Key Methods**:
  - `sample_count()`: Number of samples per channel.
  - `channel_count()`: Number of channels.
  - `audio_params()`: Associated format metadata (`AudioParams`).
  - `data(int channel)`: Raw pointer to channel samples.
  - `to_raw_ptrs()`: Returns `std::vector<float*>` for multi-channel processing.
  - `transform_volume(float factor)`: Multiplies all samples across all channels by a scalar.
  - `transform_volume_for_channel(int channel, float volume)`: Per-channel scaling.
  - `silence()`, `clamp()`, `reverse()`, `speed(double)`: In-place buffer transformations.

#### `olive::core::AudioParams`
- **Definition**: `ext/core/include/olive/core/render/audioparams.h`
- **Attributes**:
  - `sample_rate()`: Integer (e.g., 44100, 48000, 96000 Hz).
  - `channel_layout()`: Libavutil bitmask (`uint64_t`, e.g. `AV_CH_LAYOUT_STEREO`, `AV_CH_LAYOUT_MONO`).
  - `format()`: `SampleFormat` enum (`ext/core/include/olive/core/render/sampleformat.h`), defaults to `SampleFormat::F32P` for internal processing.
  - Conversions: `time_to_samples(rational)`, `samples_to_time(int64_t)`, `time_to_bytes()`.

### 1.2 DAG Node Engine & Audio Value Passing

- **Base Node**: `olive::Node` (`app/node/node.h`, `app/node/node.cpp`).
- **Data Value Container**: `olive::NodeValue` (`app/node/value.h`) stores variant data with type `NodeValue::kSamples`.
- **Evaluation Mechanism**:
  1. **DAG Traversal**: `NodeTraverser` (`app/node/traverser.h`, `traverser.cpp`) and `RenderProcessor` (`app/render/renderprocessor.h`, `renderprocessor.cpp`) call `Node::Value(const NodeValueRow &values, const NodeGlobals &globals, NodeValueTable *table)` on nodes.
  2. **Static vs. Animated Execution**:
     - **Static Processing (Instant)**: If all inputs to an audio node are static (no keyframes, verified via `node->IsInputStatic(...)`), the node transforms the `SampleBuffer` directly inside `Value()` and immediately pushes the result:
       ```cpp
       table->Push(NodeValue::kSamples, QVariant::fromValue(buffer), this);
       ```
     - **Dynamic Processing (Per-sample / Animated)**: If an input is animated (has keyframes), the node cannot apply a constant scalar. It encapsulates the input buffer into a `SampleJob` (`app/render/job/samplejob.h`):
       ```cpp
       SampleJob job(globals.time(), kSamplesInput, value);
       job.Insert(kVolumeInput, value);
       table->Push(NodeValue::kSamples, QVariant::fromValue(job), this);
       ```
     - `NodeTraverser::ResolveJobs` detects `val.canConvert<SampleJob>()`, allocates an output buffer, and dispatches to `RenderProcessor::ProcessSamples(destination, node, range, job)`.
     - `RenderProcessor::ProcessSamples` iterates sample-by-sample (`i = 0` to `sample_count - 1`), evaluates the parameter at that exact rational time, and invokes:
       ```cpp
       node->ProcessSamples(value_db, job.samples(), destination, i);
       ```

### 1.3 Existing Audio Nodes

Currently, only two dedicated audio processing nodes exist in `app/node/audio/`:
1. **`VolumeNode`**:
   - Files: `app/node/audio/volume/volume.h`, `volume.cpp`
   - Inherits: `MathNodeBase` (`app/node/math/math/mathbase.h`)
   - Inputs: `kSamplesInput` ("samples_in"), `kVolumeInput` ("volume_in", float, default 1.0, dB view).
   - In `Value()`: If static, calls `buffer.transform_volume(volume)`. If animated, pushes `SampleJob`.
2. **`PanNode`**:
   - Files: `app/node/audio/pan/pan.h`, `pan.cpp`
   - Inherits: `Node`
   - Inputs: `kSamplesInput` ("samples_in"), `kPanningInput` ("panning_in", float, range -1.0 to +1.0).
   - In `Value()`: Only supports stereo (2 channels). Scales channel 0 by `(1.0 - pan)` when panning right, or channel 1 by `(1.0 + pan)` when panning left.

### 1.4 End-to-End Audio Data Flow

```
[Media File on Disk]
        │
        ▼ (FFmpeg / libavcodec)
[Decoder::RetrieveAudio]  <-- decodes audio packets into planar float
        │
        ▼
   [ClipBlock]  <-- handles in/out points, speed (AudioProcessor tempo), reverse
        │
        ▼
[Track::ProcessAudioTrack]  <-- collects active clip buffers into track buffer
        │
        ▼
   [MathNode] (kOpAdd)  <-- sums multiple audio track buffers together
        │
        ▼
  [Sequence / ViewerOutput]  <-- root viewer node
        │
        ▼ (RenderProcessor on Worker Thread)
[AudioPlaybackCache]  <-- writes segment files to disk cache
[AudioWaveformCache]  <-- writes min/max visual waveform summaries
        │
        ▼ (ViewerWidget::QueueNextAudioBuffer every 250ms)
[AudioProcessor::Convert]  <-- packs planar float into output format QByteArray
        │
        ▼
[AudioManager::PushToOutput]
        │
        ▼ (QMutex protected buffer)
[PreviewAudioDevice::write]
        │
        ▼ (PortAudio real-time thread callback)
[OutputCallback] --> [PreviewAudioDevice::read] --> [Sound Hardware / ALSA / PulseAudio]
```

---

## 2. Parametric Equalizer Node Architecture & Implementation

### 2.1 Existing DSP & Filter Code Status

Olive has **zero biquad, IIR, FIR, or equalizer filter implementations**.
- `AudioProcessor` (`app/audio/audioprocessor.h`, `audioprocessor.cpp`) interacts with FFmpeg's `libavfilter`, but only constructs:
  - `abuffer` (audio buffer source)
  - `atempo` (time-stretching / pitch preservation)
  - `aformat` (format negotiation)
  - `abuffersink` (audio buffer sink)
- Olive does **not** link against any specialized DSP libraries (such as Faust, JUCE, or LiquidDSP).

### 2.2 Adding an Audio Node to the Node Library

Olive's node discovery and instantiation is centralized in `NodeFactory`:
1. **Header & Source Location**: Place new node in `app/node/audio/equalizer/equalizer.h` and `equalizer.cpp`.
2. **Registration in `NodeFactory`**:
   - `app/node/factory.h`: Add `kAudioEqualizer` to `enum InternalID`.
   - `app/node/factory.cpp`: Include `audio/equalizer/equalizer.h`, add case to `CreateFromFactoryIndex`:
     ```cpp
     case kAudioEqualizer:
       return new EqualizerNode();
     ```
3. **Build System**: Add `add_subdirectory(equalizer)` in `app/node/audio/CMakeLists.txt`.
4. **Node Metadata**:
   - `id()`: `QStringLiteral("org.olivevideoeditor.Olive.equalizer")`
   - `Category()`: `{kCategoryFilter}`
   - Flags: `SetFlag(kAudioEffect); SetEffectInput(kSamplesInput);`

### 2.3 Mathematical Formulation: Robert Bristow-Johnson Audio EQ Cookbook

A parametric equalizer is built from second-order IIR filters (biquads). The difference equation in Direct Form I or Transposed Direct Form II is:
$$y[n] = b_0 x[n] + b_1 x[n-1] + b_2 x[n-2] - a_1 y[n-1] - a_2 y[n-2]$$
(where all coefficients are normalized by $a_0$).

Given:
- $F_s$: Sample rate (from `input.audio_params().sample_rate()`)
- $f_0$: Center frequency in Hz (clamped to $10 \le f_0 \le 0.49 \cdot F_s$ to prevent Nyquist instability)
- $G$: Gain in decibels ($\text{dB}$)
- $Q$: Quality factor ($0.1 \le Q \le 10.0$, default $0.707$)
- Intermediate variables:
  $$A = 10^{G / 40} = \sqrt{10^{G / 20}}$$
  $$\omega_0 = 2\pi \frac{f_0}{F_s}, \quad \sin(\omega_0), \quad \cos(\omega_0)$$
  $$\alpha = \frac{\sin(\omega_0)}{2Q}$$

#### Filter Types:
1. **Peaking / Bell EQ** (Main parametric band):
   $$b_0 = 1 + \alpha \cdot A, \quad b_1 = -2\cos(\omega_0), \quad b_2 = 1 - \alpha \cdot A$$
   $$a_0 = 1 + \frac{\alpha}{A}, \quad a_1 = -2\cos(\omega_0), \quad a_2 = 1 - \frac{\alpha}{A}$$
2. **Low Shelf**:
   $$b_0 = A \left((A+1) - (A-1)\cos(\omega_0) + 2\sqrt{A}\alpha\right)$$
   $$b_1 = 2A \left((A-1) - (A+1)\cos(\omega_0)\right)$$
   $$b_2 = A \left((A+1) - (A-1)\cos(\omega_0) - 2\sqrt{A}\alpha\right)$$
   $$a_0 = (A+1) + (A-1)\cos(\omega_0) + 2\sqrt{A}\alpha$$
   $$a_1 = -2 \left((A-1) + (A+1)\cos(\omega_0)\right)$$
   $$a_2 = (A+1) + (A-1)\cos(\omega_0) - 2\sqrt{A}\alpha$$
3. **High Shelf**:
   $$b_0 = A \left((A+1) + (A-1)\cos(\omega_0) + 2\sqrt{A}\alpha\right)$$
   $$b_1 = -2A \left((A-1) + (A+1)\cos(\omega_0)\right)$$
   $$b_2 = A \left((A+1) + (A-1)\cos(\omega_0) - 2\sqrt{A}\alpha\right)$$
   $$a_0 = (A+1) - (A-1)\cos(\omega_0) + 2\sqrt{A}\alpha$$
   $$a_1 = 2 \left((A-1) - (A+1)\cos(\omega_0)\right)$$
   $$a_2 = (A+1) - (A-1)\cos(\omega_0) - 2\sqrt{A}\alpha$$
4. **Low Pass (High Cut)**:
   $$b_0 = \frac{1 - \cos(\omega_0)}{2}, \quad b_1 = 1 - \cos(\omega_0), \quad b_2 = \frac{1 - \cos(\omega_0)}{2}$$
   $$a_0 = 1 + \alpha, \quad a_1 = -2\cos(\omega_0), \quad a_2 = 1 - \alpha$$
5. **High Pass (Low Cut)**:
   $$b_0 = \frac{1 + \cos(\omega_0)}{2}, \quad b_1 = -(1 + \cos(\omega_0)), \quad b_2 = \frac{1 + \cos(\omega_0)}{2}$$
   $$a_0 = 1 + \alpha, \quad a_1 = -2\cos(\omega_0), \quad a_2 = 1 - \alpha$$
6. **Notch**:
   $$b_0 = 1, \quad b_1 = -2\cos(\omega_0), \quad b_2 = 1$$
   $$a_0 = 1 + \alpha, \quad a_1 = -2\cos(\omega_0), \quad a_2 = 1 - \alpha$$

### 2.4 Structure of `EqualizerNode`

A 4-band or 6-band parametric equalizer configuration is recommended:
- **Band 1**: Low Shelf / High Pass (Default: 80 Hz, Low Shelf)
- **Band 2**: Peaking Bell (Default: 250 Hz, Q 1.0)
- **Band 3**: Peaking Bell (Default: 1000 Hz, Q 1.0)
- **Band 4**: Peaking Bell (Default: 4000 Hz, Q 1.0)
- **Band 5**: High Shelf / Low Pass (Default: 12000 Hz, High Shelf)

Parameters per band $k$:
- `band{k}_enable`: `NodeValue::kBoolean` (default true)
- `band{k}_type`: `NodeValue::kCombo` (Low Shelf, Peaking, High Shelf, High Pass, Low Pass, Notch)
- `band{k}_freq`: `NodeValue::kFloat` (20 Hz - 20000 Hz, log slider)
- `band{k}_gain`: `NodeValue::kFloat` (-24.0 dB to +24.0 dB, `FloatSlider::kDecibel`)
- `band{k}_q`: `NodeValue::kFloat` (0.1 to 10.0, default 0.707 / 1.0)

**Processing Optimization**:
- If all parameters are static: Calculate coefficients once; run Direct Form II filter across each planar channel array using inner loops vectorized by the compiler (`-O3 -ffast-math`).
- State management: State ($x_1, x_2, y_1, y_2$) per channel is initialized to zero at block boundaries or maintained across continuous processing chunks.

---

## 3. Track Audio Mixer UI & Track Management

### 3.1 Timeline & Sequence Track Representation

In Olive's architecture:
- `Sequence` (`app/node/project/sequence/sequence.h`) owns three `TrackList` objects:
  - `track_list(Track::kVideo)`
  - `track_list(Track::kAudio)`
  - `track_list(Track::kSubtitle)`
- `TrackList` (`app/node/output/track/tracklist.h`):
  - Manages `QVector<Track*> track_cache_`.
  - Emits `TrackAdded(Track*)`, `TrackRemoved(Track*)`, `TrackListChanged()`.
- `Track` (`app/node/output/track/track.h`):
  - `type()`: `Track::kAudio`, `Track::kVideo`, `Track::kSubtitle`.
  - `Index()`: 0-based index within its track type.
  - `Blocks()`: Ordered sequence of `Block*` (e.g. `ClipBlock`, `GapBlock`).
  - Inputs: `kBlockInput` ("blocks_in"), `kMutedInput` ("muted_in"), `kArrayMapInput`.

### 3.2 Current State of Track Levels, Pan, Mute, Solo

1. **Mute**: Fully implemented in `Track`.
   - Track has `kMutedInput` (`app/node/output/track/track.cpp` line 41).
   - In `Track::GetActiveElementsAtTime(...)` (line 105):
     ```cpp
     if (IsMuted() || blocks_.empty() || r.in() >= track_length() || r.out() <= 0) {
       return ActiveElements::kNoElements;
     }
     ```
     When muted, the track returns no elements, so no audio is fetched or rendered for that track.
2. **Solo**:
   - In `TrackViewItem::TrackViewItem` (`app/widget/timelinewidget/trackview/trackviewitem.cpp` line 67):
     ```cpp
     /*solo_button_ = CreateMSLButton(tr("S"), Qt::yellow);
     layout->addWidget(solo_button_);*/
     ```
     The solo button was commented out during early UI construction and never hooked up to any backend logic.
3. **Volume and Pan**:
   - `Track` has **no volume or pan inputs**.
   - Track volume and pan do not exist in the timeline data model.
   - Volume and pan currently can only be adjusted per-clip by inserting `VolumeNode` and `PanNode` manually into the node graph.

### 3.3 Proposed Architectural Enhancement: Native Track Audio Controls

Instead of inserting artificial intermediate nodes into the user's DAG for every audio track, **the clean and standard Olive architecture is to add native inputs to `Track`**:
1. **Inputs in `Track`**:
   - `kVolumeInput` ("volume_in", `NodeValue::kFloat`, default 1.0, dB view, min 0.0, max 4.0 / +12dB).
   - `kPanInput` ("pan_in", `NodeValue::kFloat`, default 0.0, range -1.0 to +1.0).
   - `kSoloInput` ("solo_in", `NodeValue::kBoolean`, default false, not keyframable).
2. **Execution in `Track::ProcessAudioTrack`**:
   In `app/node/output/track/track.cpp`:
   - After copying active clip samples into `block_range_buffer`:
     ```cpp
     float vol = GetStandardValue(kVolumeInput).toFloat();
     if (!qFuzzyCompare(vol, 1.0f)) {
       block_range_buffer.transform_volume(vol);
     }
     float pan = GetStandardValue(kPanInput).toFloat();
     if (!qFuzzyIsNull(pan) && block_range_buffer.channel_count() == 2) {
       if (pan > 0.0f) {
         block_range_buffer.transform_volume_for_channel(0, 1.0f - pan);
       } else {
         block_range_buffer.transform_volume_for_channel(1, 1.0f + pan);
       }
     }
     ```
3. **Solo Management**:
   In `TrackList` or `Sequence`:
   - Check if any audio track in `track_list(Track::kAudio)` has `solo == true`.
   - If at least one track is soloed: any track whose `solo == false` is treated as muted (`return ActiveElements::kNoElements;`).
   - If no track is soloed: standard mute flags govern playback.
4. **Project Persistence**:
   - Because inputs in `Node` are automatically saved/restored in Olive's XML serializer (`app/node/node.cpp` `SaveStandardValues`/`LoadStandardValues`), adding `kVolumeInput`, `kPanInput`, and `kSoloInput` preserves backward and forward compatibility automatically without modifying the project schema file!

### 3.4 Track Audio Mixer Panel Placement in Qt6 UI

1. **Panel Class**: Create `AudioMixerPanel` inheriting `PanelWidget` (`app/panel/panel.h`).
2. **KDDockWidgets Integration**:
   - In `app/window/mainwindow/mainwindow.h`: Add `AudioMixerPanel* audio_mixer_panel_;`.
   - In `app/window/mainwindow/mainwindow.cpp`:
     ```cpp
     audio_mixer_panel_ = new AudioMixerPanel();
     ```
   - In `MainWindow::SetDefaultLayout()`:
     Dock beside or tabbed with `audio_monitor_panel_` or `timeline_panels_.first()`:
     ```cpp
     audio_monitor_panel_->addDockWidgetAsTab(audio_mixer_panel_);
     ```
   - `PanelManager::instance()->panels()` automatically enumerates `AudioMixerPanel` and adds a toggle action to the `Window` menu.
3. **Mixer UI Layout (Channel Strip Design)**:
   - Horizontal scrollable container (`QScrollArea`) holding vertical channel strips.
   - For each audio track in the active `Sequence`:
     - **Track Label**: Synchronized with `Track::GetLabelOrName()`.
     - **Mute / Solo Buttons**: Toggle buttons synced with `Track::SetMuted` and `Track::SetSolo`.
     - **Pan Slider / Knob**: Horizontal slider or knob centered at 0.0 (-1.0 to +1.0).
     - **Volume Fader**: Vertical slider mapped to decibel scale ($-\infty$ to $+6$ dB / $+12$ dB, unity gain at 0 dB).
     - **Stereo VU / Peak Meter**: Dual-channel LED-style vertical meter beside the fader.
     - **EQ Button / Slot**: Opens or reveals the Parametric EQ curve controls for that track.
   - **Master Bus Strip**:
     - Pinned to the right side of the mixer.
     - Controls sequence master output level, master pan, and master stereo VU meter.

---

## 4. VU Meters / Audio Metering & Thread Safety

### 4.1 How Audio Levels Are Monitored in Olive

Currently, `AudioMonitor` (`app/widget/audiomonitor/audiomonitor.cpp`) monitors audio using two different techniques:
1. **Scrubbing**:
   - Called directly via `AudioMonitor::PushSampleBufferOnAll(samples)` in `ViewerWidget::ReceivedAudioBufferForScrubbing()` (`app/widget/viewer/viewer.cpp` line 821).
   - Computes peak amplitude via `AudioVisualWaveform::SumSamples` and renders onto OpenGL canvas.
2. **Playback**:
   - When playback starts, `ViewerWidget::Play()` calls:
     ```cpp
     AudioMonitor::StartWaveformOnAll(GetConnectedNode()->GetConnectedWaveform(), ...);
     ```
   - During playback, `AudioMonitor` reads from the pre-generated `AudioWaveformCache` file:
     ```cpp
     AudioVisualWaveform::Sample sum = waveform_->GetSummaryFromTime(waveform_time_, length);
     ```
   - **Limitation**: This only works if waveforms have already been cached to disk! It does not reflect live track changes, track mutes, or per-track dynamics during editing.

### 4.2 Thread-Safe Metering Architecture for the Mixer

In real-time audio systems, computing levels must never cause audio stutter (buffer underflow/xrun) and must never lock the GUI.

#### The Three Threads Involved:
1. **PortAudio Audio Output Thread** (`OutputCallback`): High priority real-time thread. Must NEVER block, allocate memory (`malloc`/`new`), or call Qt methods.
2. **Worker Threads** (`RenderProcessor`): Traverses DAG, evaluates `Track::ProcessAudioTrack`, renders `SampleBuffer`s.
3. **Qt GUI Thread**: Renders VU meter bars, animates peak hold needles, responds to user mouse/touch.

#### Thread-Safe Computation Pipeline:

```
[PortAudio Audio Callback / OutputCallback]
                     │
     (Per-audio-buffer sample pass: ~128-512 frames)
     Computes: Peak = max(|s|), SumSq = sum(s*s)
                     │
                     ▼
       [Lock-Free Atomic Meter Store]
      std::atomic<float> peak_l, peak_r;
      std::atomic<float> rms_l,  rms_r;
                     │
   (No locks, memory_order_relaxed / release)
                     │
                     ▼
  [Qt GUI Thread: QTimer @ 30-60 Hz (16-33ms)]
   - Atomic load (memory_order_relaxed)
   - Convert to dB: 20 * log10(val)
   - Apply Ballistics:
       Instantaneous attack
       Decay: 20 dB / second
       Peak Hold: 1.5 second hold with 30 dB/s decay
   - Repaint meter bars via QPainter
```

#### Per-Track Metering:
For individual tracks in the timeline:
- Since tracks are pre-rendered into `SampleBuffer` during `RenderProcessor::ProcessAudioFootage` and `Track::ProcessAudioTrack`, every `Track` already has an associated `AudioWaveformCache` containing `AudioVisualWaveform::SamplePerChannel` (storing `min` and `max` sample peaks per time interval).
- For synchronized playback metering: The mixer queries the track's `waveform_cache()` at the current playback playhead:
  ```cpp
  AudioVisualWaveform::Sample s = track->waveform_cache()->GetSummaryFromTime(playhead, window_length);
  ```
- For live scrubbing and instant adjustment: `Track::ProcessAudioTrack` publishes the latest RMS/peak values into an atomic track meter structure stored on `Track`.

---

## 5. Thread Safety & Audio Playback Engine

### 5.1 Olive's Threading Model

Olive uses a clear multi-threaded separation of concerns:
1. **Main / GUI Thread**:
   - All UI widgets (`MainWindow`, `TimelineWidget`, `AudioMonitorPanel`).
   - Project graph manipulation (`Node`, `Sequence`, `Track`) driven by `QUndoStack`.
2. **Worker Thread Pool (`RenderManager`)**:
   - Multi-threaded rendering via `QThreadPool` / `RenderProcessor`.
   - **Isolation Guarantee**: `PreviewAutoCacher` uses `ProjectCopier` (`app/render/previewautocacher.cpp` line 46) to make an isolated deep-copy of the active node graph. Background render jobs execute on copied nodes, preventing data races against user edits on the GUI thread.
3. **Audio Output Thread (PortAudio)**:
   - Managed by `AudioManager` (`app/audio/audiomanager.cpp`).
   - `Pa_OpenStream` registers `OutputCallback` (line 58).
   - PortAudio spawns an OS-level audio thread (e.g., ALSA/PulseAudio/JACK thread on Linux).

### 5.2 Locks and Synchronization Primitives

| Component | Synchronization Primitive | Purpose |
|---|---|---|
| `PreviewAudioDevice` | `QMutex lock_` (`QMutexLocker`) | Protects `buffer_` (`QByteArray`) and `bytes_read_` between `PushToOutput` (GUI thread) and `OutputCallback` (audio thread). |
| `PlaybackCache` | `QMutex mutex_` (`app/render/playbackcache.h` line 89) | Protects disk cache index reading and writing across worker threads. |
| `RenderTicketWatcher` | Qt Signal / Slot Mechanism | Asynchronous notification when background render jobs finish; marshals results back to the GUI event loop. |
| `PreviewAutoCacher` | `delayed_requeue_timer_` | Debounces rapid timeline changes (`AutoCacheDelay`) before launching background audio/video re-renders. |

### 5.3 Guidelines for Real-Time Safety in Audio Additions

- **No Allocations in Audio Path**: Biquad filter processing loops must operate on pre-allocated buffers. Avoid `std::vector::push_back`, `QByteArray::resize`, or `new` in the processing loop.
- **Lock-Free Communication**: Meter values passed from the audio thread or worker threads to the GUI thread must use `std::atomic<float>` or `std::atomic<uint32_t>` (fixed-point dB) to avoid blocking the audio callback.

---

## 6. Unit Testing Architecture & Conventions

### 6.1 Test Infrastructure Overview

Olive features a custom test harness integrated with CMake and CTest:
- **Test Discovery**: `tests/CMakeLists.txt` provides the CMake function `olive_add_test(GROUP NAME SOURCE)`.
- **Harness Header**: `tests/testutil.h`:
  - `OLIVE_ADD_TEST(TestName)` expands to `int Test##TestName()`
  - `OLIVE_ASSERT(condition)` returns `__LINE__` on failure
  - `OLIVE_ASSERT_EQUAL(val1, val2)` prints mismatch and returns `__LINE__`
  - `OLIVE_TEST_END` returns `OLIVE_TEST_SUCCESS` (-1)
- **Automatic Runner Generation**: `tests/CMakeLists.txt` parses test files for `OLIVE_ADD_TEST(...)`, automatically generates `main()`, initializes `QCoreApplication`, and prints test results.

### 6.2 Existing Audio Unit Tests Reference

The primary reference for audio testing in Olive is `tests/timeline/tempo-tests.cpp`:
- Generates known audio signals using `FillSine(&input)` (440 Hz sine wave).
- Constructs synthetic `AudioParams` (`AV_CH_LAYOUT_STEREO`, 48000 Hz, `F32P`).
- Builds mock `Track` and `ClipBlock` nodes, wraps inputs in `NodeValueArray` and `NodeValueRow`.
- Evaluates `track.Value(...)` and asserts resulting sample counts and sample values within epsilon tolerance (`std::fabs(result.data(c)[i] - wanted) < 1e-6f`).

### 6.3 Conventions for New Audio Tests

New audio unit tests should be added in `tests/audio/` (or `tests/timeline/`):
1. **Parametric Equalizer Tests**:
   - **Flat Response Test**: Verify that with gain = 0 dB or all bands bypassed, input buffer matches output buffer within $10^{-6}$.
   - **Low-pass / High-pass Attenuation Test**: Feed a combination of 100 Hz and 10,000 Hz tones. Verify high-frequency attenuation when a 1 kHz low-pass filter is applied.
   - **Peaking Gain Verification**: Test boosting 1 kHz by +6 dB ($2\times$ amplitude) and verify peak amplitude matches expected gain factor.
   - **Impulse Response Stability**: Feed a unit impulse $[1.0, 0.0, 0.0, \dots]$ and ensure output does not produce NaN, infinity, or explode across 48,000 samples.
2. **Mixer Track Math Tests**:
   - **Track Volume Scaling**: Verify volume at 0.5 (-6 dB) scales samples by 0.5.
   - **Track Stereo Panning**: Verify pan at +1.0 completely silences channel 0 and preserves channel 1.
   - **Track Solo Logic**: With track 1 soloed and track 2 active, verify track 2 produces silence.
3. **Running the Tests**:
   - Quick check: `ctest --test-dir build-linux-asan -R <test_name> --output-on-failure`
   - Gauntlet check: `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`

---

## 7. Concrete File & Class Blueprint

| Component | Target File | Base Class / Namespace | Primary Responsibility |
|---|---|---|---|
| **Parametric EQ Node** | `app/node/audio/equalizer/equalizer.h`, `.cpp` | `olive::Node` | Multi-band biquad IIR filter node for clips and tracks. |
| **Biquad DSP Engine** | `app/node/audio/equalizer/biquad.h`, `.cpp` | `olive::Biquad` | Pure C++17 RBJ Audio EQ Cookbook biquad implementation. |
| **Track Volume & Pan** | `app/node/output/track/track.h`, `.cpp` | `olive::Track` | Add `kVolumeInput`, `kPanInput`, `kSoloInput` and in-place sample transformation. |
| **Track Solo Logic** | `app/node/output/track/tracklist.h`, `.cpp` | `olive::TrackList` | Track-wide solo coordinator; mutes non-soloed tracks. |
| **Track Audio Mixer Panel** | `app/panel/audiomixer/audiomixerpanel.h`, `.cpp` | `olive::PanelWidget` | KDDockWidgets dock panel housing channel strips. |
| **Mixer Channel Strip Widget** | `app/widget/audiomixer/channelstrip.h`, `.cpp` | `QWidget` | Vertical fader, pan slider, M/S buttons, track label. |
| **Stereo VU Meter Widget** | `app/widget/audiomixer/vumeter.h`, `.cpp` | `QWidget` | Smooth peak/RMS vertical meter with dB scale and clip indicators. |
| **Atomic Level Monitor** | `app/audio/audiometer.h`, `.cpp` | `olive::AudioMeter` | Lock-free peak/RMS aggregator between audio thread and UI. |
| **Audio Unit Tests** | `tests/audio/equalizer-tests.cpp`, `mixer-tests.cpp` | CTest / `testutil.h` | Comprehensive verification of EQ filters, pan, volume, and solo. |

---

## 8. Recommendations for Implementation Phase

1. **Keep DSP Pure and Dependency-Free**:
   The Robert Bristow-Johnson biquad filter formulas require only `<cmath>` and `<vector>`. Do not introduce heavy third-party audio dependencies. Pure C++17 guarantees 100% portability, predictable performance, and zero issues with AddressSanitizer.
2. **Adopt Native Track Inputs**:
   Do not dynamically inject hidden `VolumeNode` and `PanNode` instances into the user's DAG when adding tracks. Instead, declare `kVolumeInput`, `kPanInput`, and `kSoloInput` directly in `Track`. This ensures project files serialize cleanly, undo/redo works natively, and performance is maximized.
3. **Decouple Audio Thread from Metering UI**:
   Never use Qt signals or mutex locks inside PortAudio's `OutputCallback`. Use `std::atomic<float>` for instantaneous peak/RMS transfer, sampled at 30-60 Hz via a GUI `QTimer`.
4. **Pass the Gauntlet Gate**:
   Ensure all new classes have virtual destructors, initialize all scalar members in constructors to avoid uninitialized memory reads under MemorySanitizer/AddressSanitizer, and verify with `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`.
