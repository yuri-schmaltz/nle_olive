# Technical Report & Implementation Blueprint: Track Audio Mixing Controls (Volume, Pan, Solo)

**Target Component**: Track Audio Mixing Engine  
**Milestone**: M1 (Audio Engine & Parametric EQ Node)  
**Author**: `explorer_track_audio` (Teamwork Explorer)  
**Date**: September 20, 2026  
**Target Repository**: Olive Video Editor (`C++17 / Qt6`)  

---

## 1. Executive Summary

This report delivers the comprehensive technical architectural blueprint for adding native **Volume**, **Pan**, and **Solo** audio mixing controls to Olive's `Track` engine (`app/node/output/track/track.h`, `track.cpp`, `tracklist.h`, `tracklist.cpp`).

### Core Findings & Architectural Decisions
1. **Direct Native Inputs on `Track`**:
   Rather than introducing artificial intermediate nodes into the user's Directed Acyclic Graph (DAG), `Track` natively hosts `kVolumeInput` (`"volume_in"`, float, default 1.0, dB slider view), `kPanInput` (`"pan_in"`, float, default 0.0, range -1.0 to +1.0, percentage view), and `kSoloInput` (`"solo_in"`, bool, default false).
2. **High-Performance In-Place Evaluation**:
   In `Track::ProcessAudioTrack`, after copying active clip samples into `block_range_buffer`, volume scaling and stereo balance panning are applied directly using existing SIMD-accelerated methods on `olive::core::SampleBuffer` (`transform_volume` and `transform_volume_for_channel`).
3. **Robust Fallback Handling for Tests & Direct Evaluation**:
   Unit tests such as `tests/timeline/tempo-tests.cpp` pass synthetic `NodeValueRow` instances containing only `kBlockInput`. `Track::ProcessAudioTrack` safely queries `value.contains(...) ? value[k...].toDouble() : GetStandardValue(k...).toFloat()`, preventing unintended silencing of legacy test suites.
4. **Two-Stage Solo Muting Coordination**:
   Solo logic is managed via `TrackList` and `Track::IsEffectivelyMuted()`. When any audio track in a sequence is soloed, non-soloed tracks are bypassed at:
   - **Stage 1 (`GetActiveElementsAtTime`)**: Returns `ActiveElements::kNoElements`, eliminating block traversal, clip decoding, and tempo stretching.
   - **Stage 2 (`ProcessAudioTrack`)**: If called directly, immediately emits a silent buffer.
5. **Cross-Track Cache Invalidation on Solo Change**:
   When `kSoloInput` changes on any track, cache invalidation is propagated to all sibling audio tracks in the sequence, ensuring immediate timeline preview and waveform cache updates.
6. **Zero Project File Schema Breakage**:
   Olive's generic XML node serialization automatically serializes and restores standard values. Missing inputs in legacy project files retain default values (`vol=1.0`, `pan=0.0`, `solo=false`), ensuring 100% backward and forward compatibility.
7. **Complete Undo/Redo Fidelity**:
   All three parameters seamlessly integrate with Olive's `QUndoStack` via `NodeParamSetStandardValueCommand` and `NodeInputDragger`.

---

## 2. Codebase Investigation: Audio Data Flow & DAG Traversal

### 2.1 Timeline Audio Topology
In Olive:
1. A `Sequence` (`app/node/project/sequence/sequence.h`) contains three `TrackList` instances corresponding to `Track::kVideo`, `Track::kAudio`, and `Track::kSubtitle`.
2. For audio tracks, `TimelineAddTrackCommand` (`app/timeline/timelineundogeneral.cpp:78-120`) builds a binary tree of `MathNode` (addition) nodes connecting each audio track output to `Sequence::kSamplesInput`:
   ```
   [Track A0] ──┐
                ├──> [MathNode: Add] ──┐
   [Track A1] ──┘                      ├──> [Sequence::kSamplesInput]
                                       │
   [Track A2] ─────────────────────────┘
   ```
3. In addition to DAG output connections, each track is connected to an array input on `Sequence`:
   `Sequence::track_in_1` (`kTrackInputFormat.arg(Track::kAudio)`).
   When connected, `TrackList::TrackConnected` (`app/node/output/track/tracklist.cpp:48-99`) sets:
   ```cpp
   track->set_type(type_);       // Track::kAudio
   track->set_sequence(parent());// Pointer to owning Sequence
   ```
   Therefore, every track in a project has direct access to its parent `Sequence` and sibling tracks via `sequence()->track_list(Track::kAudio)->GetTracks()`.

### 2.2 `Track::ProcessAudioTrack` Audio Pipeline
Currently in `app/node/output/track/track.cpp:628-726`:
```
[globals.time()] ──> TimeRange
                         │
                         ▼
        [Allocate block_range_buffer] (globals.aparams(), length)
        [block_range_buffer.silence()]
                         │
                         ▼
             [Loop active blocks in row]
                         │
        ┌────────────────┴────────────────┐
        ▼                                 ▼
   [ClipBlock]                       [Gap / other]
        │                                 │
   Apply tempo / speed / reverse          │
        │                                 │
        ▼                                 ▼
   Copy samples to destination offset in block_range_buffer
                         │
                         ▼
           *MISSING TRACK MIXING HOOK*
   (Track volume, pan, and solo are currently absent)
                         │
                         ▼
        [Push block_range_buffer to table]
```

### 2.3 `SampleBuffer` DSP Capabilities
`olive::core::SampleBuffer` (`ext/core/src/render/samplebuffer.cpp:149-175`) already provides optimized multi-channel DSP methods:
- `transform_volume(float factor)`: Applies scalar gain across all channels using SSE/NEON SIMD intrinsics (`_mm_mul_ps`).
- `transform_volume_for_channel(int channel, float volume)`: Applies SIMD scalar gain to a specific channel index.
- `silence()`: Fast zeroing of planar float buffers via `memset`.

---

## 3. Detailed Technical Design & Implementation Blueprint

### 3.1 Adding Inputs to `Track`

#### 3.1.1 Input Constants & Property Configuration
In `app/node/output/track/track.h`:
```cpp
public:
  static const QString kBlockInput;
  static const QString kMutedInput;
  static const QString kArrayMapInput;
  static const QString kVolumeInput;  // "volume_in"
  static const QString kPanInput;     // "pan_in"
  static const QString kSoloInput;    // "solo_in"
```

In `app/node/output/track/track.cpp`:
```cpp
const QString Track::kVolumeInput = QStringLiteral("volume_in");
const QString Track::kPanInput = QStringLiteral("pan_in");
const QString Track::kSoloInput = QStringLiteral("solo_in");
```

In `Track::Track()` constructor:
```cpp
// Track volume: float, default 1.0 (0 dB), min 0.0 (-inf dB), decibel slider representation
AddInput(kVolumeInput, NodeValue::kFloat, 1.0,
         InputFlags(kInputFlagNotConnectable | kInputFlagNotKeyframable));
SetInputProperty(kVolumeInput, QStringLiteral("min"), 0.0);
SetInputProperty(kVolumeInput, QStringLiteral("view"), FloatSlider::kDecibel);

// Track stereo pan: float, default 0.0 (Center), range [-1.0, 1.0], percentage representation
AddInput(kPanInput, NodeValue::kFloat, 0.0,
         InputFlags(kInputFlagNotConnectable | kInputFlagNotKeyframable));
SetInputProperty(kPanInput, QStringLiteral("min"), -1.0);
SetInputProperty(kPanInput, QStringLiteral("max"), 1.0);
SetInputProperty(kPanInput, QStringLiteral("view"), FloatSlider::kPercentage);

// Track solo: boolean, default false
AddInput(kSoloInput, NodeValue::kBoolean, false,
         InputFlags(kInputFlagNotConnectable | kInputFlagNotKeyframable));
```

#### 3.1.2 Public Accessors & Qt Slots
In `app/node/output/track/track.h`:
```cpp
public:
  float GetVolume() const;
  float GetPan() const;
  bool IsSoloed() const;
  bool IsEffectivelyMuted() const;

public slots:
  void SetVolume(float v);
  void SetPan(float p);
  void SetSolo(bool e);

signals:
  void VolumeChanged(float v);
  void PanChanged(float p);
  void SoloChanged(bool e);
```

In `app/node/output/track/track.cpp`:
```cpp
float Track::GetVolume() const
{
  return GetStandardValue(kVolumeInput).toFloat();
}

float Track::GetPan() const
{
  return GetStandardValue(kPanInput).toFloat();
}

bool Track::IsSoloed() const
{
  return GetStandardValue(kSoloInput).toBool();
}

void Track::SetVolume(float v)
{
  SetStandardValue(kVolumeInput, v);
}

void Track::SetPan(float p)
{
  SetStandardValue(kPanInput, p);
}

void Track::SetSolo(bool e)
{
  SetStandardValue(kSoloInput, e);
}
```

#### 3.1.3 Retranslation
In `Track::Retranslate()`:
```cpp
void Track::Retranslate()
{
  super::Retranslate();

  SetInputName(kBlockInput, tr("Blocks"));
  SetInputName(kMutedInput, tr("Muted"));
  SetInputName(kVolumeInput, tr("Volume"));
  SetInputName(kPanInput, tr("Pan"));
  SetInputName(kSoloInput, tr("Solo"));
}
```

---

### 3.2 Volume & Pan Application in `Track::ProcessAudioTrack`

#### 3.2.1 Mathematical Formulation of Pan Law
Olive utilizes a linear balance pan law matching `PanNode`:
Let $p \in [-1.0, 1.0]$ denote the pan parameter, and $V \ge 0.0$ denote linear volume gain.
For a stereo buffer with Left channel $L$ (channel 0) and Right channel $R$ (channel 1):

1. **Volume Scaling**:
   $$L' = L \cdot V, \quad R' = R \cdot V$$

2. **Stereo Panning**:
   $$\begin{cases}
   L'' = L' \cdot (1.0 - p), \quad R'' = R' & \text{if } p > 0.0 \text{ (pan right)} \\
   L'' = L', \quad R'' = R' \cdot (1.0 + p) & \text{if } p < 0.0 \text{ (pan left)} \\
   L'' = L', \quad R'' = R' & \text{if } p = 0.0 \text{ (center)}
   \end{cases}$$

At $p = +1.0$ (hard right), Left channel gain is $1.0 - 1.0 = 0.0$ (complete silence).  
At $p = -1.0$ (hard left), Right channel gain is $1.0 + (-1.0) = 0.0$ (complete silence).  
If $channel\_count \ne 2$ (e.g., mono sequence), pan balance is not applicable and is bypassed.

#### 3.2.2 Code Implementation in `Track::ProcessAudioTrack`
```cpp
void Track::ProcessAudioTrack(const NodeValueRow &value, const NodeGlobals &globals, NodeValueTable *table) const
{
  const TimeRange &range = globals.time();

  SampleBuffer block_range_buffer(globals.aparams(), range.length());
  block_range_buffer.silence();

  // Early exit if track is effectively muted by mute flag or solo state
  if (IsEffectivelyMuted()) {
    table->Push(NodeValue::kSamples, QVariant::fromValue(block_range_buffer), this);
    return;
  }

  // Loop through active blocks retrieving their audio
  NodeValueArray arr = value[kBlockInput].toArray();

  for (auto it=arr.cbegin(); it!=arr.cend(); it++) {
    Block *b = blocks_.at(GetCacheIndexFromArrayIndex(it->first));

    TimeRange range_for_block(qMax(b->in(), range.in()),
                              qMin(b->out(), range.out()));

    if (range_for_block.out() <= range_for_block.in()) {
      continue;
    }

    qint64 destination_offset = globals.aparams().time_to_samples(range_for_block.in() - range.in());
    qint64 max_dest_sz = globals.aparams().time_to_samples(range_for_block.length());

    SampleBuffer samples_from_this_block = it->second.toSamples();

    if (samples_from_this_block.is_allocated()) {
      if (ClipBlock *clip_cast = dynamic_cast<ClipBlock*>(b)) {
        double speed_value = clip_cast->speed();
        bool reversed = clip_cast->reverse();

        if (qIsNull(speed_value)) {
          samples_from_this_block.silence();
        } else if (!qFuzzyCompare(speed_value, 1.0)) {
          if (clip_cast->maintain_audio_pitch()) {
            AudioProcessor processor;
            const AudioParams params = samples_from_this_block.audio_params();
            AudioProcessor::Buffer out;
            bool converted = false;
            if (processor.Open(params, params, speed_value)) {
              int r = processor.Convert(samples_from_this_block.to_raw_ptrs().data(),
                                        samples_from_this_block.sample_count(), nullptr);
              if (r >= 0) {
                processor.Flush();
                r = processor.Convert(nullptr, 0, &out);
                converted = r >= 0;
              }
              if (r < 0) {
                qCritical() << "Failed to change tempo of audio:" << r;
              }
            } else {
              qCritical() << "Failed to open tempo processor";
            }

            if (!converted || out.empty() || out.front().isEmpty()) {
              continue;
            }
            const size_t nb_samples = out.front().size() / params.bytes_per_sample_per_channel();
            SampleBuffer new_samples(params, nb_samples);
            for (int i = 0; i < out.size(); i++) {
              memcpy(new_samples.data(i), out[i].constData(), out[i].size());
            }
            samples_from_this_block = new_samples;
          } else {
            samples_from_this_block.speed(speed_value);
          }
        }

        if (reversed) {
          samples_from_this_block.reverse();
        }
      }

      const qint64 available = qint64(block_range_buffer.sample_count()) - destination_offset;
      const qint64 copy_length = qMin(qMin(max_dest_sz, available),
                                     qint64(samples_from_this_block.sample_count()));
      if (copy_length <= 0) {
        continue;
      }

      for (int i=0; i<samples_from_this_block.audio_params().channel_count(); i++) {
        block_range_buffer.set(i, samples_from_this_block.data(i), destination_offset, copy_length);
      }
    }
  }

  // Retrieve track volume (with fallback for raw test rows)
  float vol = 1.0f;
  if (value.contains(kVolumeInput) && value[kVolumeInput].isValid()) {
    vol = static_cast<float>(value[kVolumeInput].toDouble());
  } else {
    vol = GetStandardValue(kVolumeInput).toFloat();
  }
  vol = std::max(0.0f, vol);

  // Apply track volume
  if (qFuzzyIsNull(vol)) {
    block_range_buffer.silence();
  } else if (!qFuzzyCompare(vol, 1.0f)) {
    block_range_buffer.transform_volume(vol);
  }

  // Retrieve track stereo pan
  float pan = 0.0f;
  if (value.contains(kPanInput) && value[kPanInput].isValid()) {
    pan = static_cast<float>(value[kPanInput].toDouble());
  } else {
    pan = GetStandardValue(kPanInput).toFloat();
  }
  pan = std::clamp(pan, -1.0f, 1.0f);

  // Apply track pan to stereo buffer
  if (!qFuzzyIsNull(pan) && block_range_buffer.channel_count() == 2 && !qFuzzyIsNull(vol)) {
    if (pan > 0.0f) {
      block_range_buffer.transform_volume_for_channel(0, 1.0f - pan);
    } else {
      block_range_buffer.transform_volume_for_channel(1, 1.0f + pan);
    }
  }

  table->Push(NodeValue::kSamples, QVariant::fromValue(block_range_buffer), this);
}
```

---

### 3.3 Solo Muting Logic Architecture

#### 3.3.1 Solo State Truth Table
Let $S_k$ be the solo state and $M_k$ be the mute state of track $k$.  
Let $H = \bigvee_{i \in \text{AudioTracks}} S_i$ indicate whether at least one audio track is soloed.  
The effective mute status $E_k$ of track $k$ is defined by:
$$E_k = M_k \lor (H \land \neg S_k)$$

| Condition | Track Muted ($M_k$) | Track Soloed ($S_k$) | Any Track Soloed ($H$) | Effective Mute ($E_k$) | Track Audio Rendered |
|---|---|---|---|---|---|
| Normal Playback | False | False | False | **False** | Full Audio |
| Muted Track | True | False | False | **True** | Bypassed (Silence) |
| Soloed Track | False | True | True | **False** | Full Audio |
| Soloed but Muted | True | True | True | **True** | Bypassed (Silence) |
| Non-Soloed Sibling | False | False | True | **True** | Bypassed (Silence) |
| Non-Soloed & Muted | True | False | True | **True** | Bypassed (Silence) |

#### 3.3.2 Methods in `TrackList`
In `app/node/output/track/tracklist.h`:
```cpp
public:
  bool HasSoloTrack() const;

signals:
  void TrackSoloChanged(Track *track, bool soloed);
```

In `app/node/output/track/tracklist.cpp`:
```cpp
bool TrackList::HasSoloTrack() const
{
  for (Track *t : track_cache_) {
    if (t && t->IsSoloed()) {
      return true;
    }
  }
  return false;
}
```

#### 3.3.3 Implementation in `Track::IsEffectivelyMuted()`
In `app/node/output/track/track.cpp`:
```cpp
bool Track::IsEffectivelyMuted() const
{
  if (IsMuted()) {
    return true;
  }

  if (sequence_ && track_type_ == Track::kAudio) {
    TrackList *tl = sequence_->track_list(Track::kAudio);
    if (tl && tl->HasSoloTrack()) {
      return !IsSoloed();
    }
  }

  return false;
}
```

#### 3.3.4 DAG Optimization in `Track::GetActiveElementsAtTime`
In `app/node/output/track/track.cpp:102-107`:
```cpp
Node::ActiveElements Track::GetActiveElementsAtTime(const QString &input, const TimeRange &r) const
{
  if (input == kBlockInput) {
    if (IsEffectivelyMuted() || blocks_.empty() || r.in() >= track_length() || r.out() <= 0) {
      return ActiveElements::kNoElements;
    }
    ...
```
When `IsEffectivelyMuted()` evaluates to true:
1. `GetActiveElementsAtTime` returns `ActiveElements::kNoElements`.
2. `NodeTraverser::ProcessInput` skips processing any array elements of `kBlockInput`.
3. Background decoder threads are not dispatched; no samples are pulled from disk cache.
4. `ProcessAudioTrack` receives an empty `arr`, zero iterations occur, and silent buffer is pushed.

#### 3.3.5 Cache Invalidation & Multi-Track Cross-Talk
When a track is soloed or unsoloed, its own parameters change. However, sibling tracks' effective mute states also toggle.
In `Track::InputValueChangedEvent`:
```cpp
void Track::InputValueChangedEvent(const QString &input, int element)
{
  Q_UNUSED(element)

  if (input == kMutedInput) {
    emit MutedChanged(IsMuted());
  } else if (input == kSoloInput) {
    emit SoloChanged(IsSoloed());

    if (sequence_ && track_type_ == Track::kAudio) {
      TrackList *tl = sequence_->track_list(Track::kAudio);
      if (tl) {
        emit tl->TrackSoloChanged(this, IsSoloed());

        // Invalidate sibling audio tracks because their effective mute status inverted
        for (Track *t : tl->GetTracks()) {
          if (t != this) {
            t->InvalidateCache(TimeRange(0, t->track_length()), kSoloInput);
          }
        }
      }
    }
  } else if (input == kVolumeInput) {
    emit VolumeChanged(GetVolume());
  } else if (input == kPanInput) {
    emit PanChanged(GetPan());
  } else if (input == kArrayMapInput) {
    if (ignore_arraymap_ > 0) {
      ignore_arraymap_--;
    } else {
      RefreshBlockCacheFromArrayMap();
    }
  }
}
```

---

### 3.4 XML Serialization & Project Compatibility

#### 3.4.1 Serialization Mechanism
In Olive, `Node::Save` (`app/node/node.cpp:1321-1341`) saves every entry in `inputs()`:
```cpp
foreach (const QString& input, this->inputs()) {
  writer->writeStartElement(QStringLiteral("input"));
  SaveInput(writer, input);
  writer->writeEndElement();
}
```
For `kVolumeInput`, `kPanInput`, and `kSoloInput`:
- `Node::SaveImmediate` creates a `<standard><track>...</track></standard>` block.
- For default parameters on an untouched track:
  ```xml
  <input id="volume_in">
    <primary>
      <standard>
        <track>1</track>
      </standard>
    </primary>
  </input>
  <input id="pan_in">
    <primary>
      <standard>
        <track>0</track>
      </standard>
    </primary>
  </input>
  <input id="solo_in">
    <primary>
      <standard>
        <track>0</track>
      </standard>
    </primary>
  </input>
  ```

#### 3.4.2 Backward Compatibility Analysis
- When opening existing Olive projects (saved before this feature):
  1. The project XML lacks `<input id="volume_in">`, `<input id="pan_in">`, and `<input id="solo_in">`.
  2. `Track::Track()` initializes these inputs with default values: `volume = 1.0`, `pan = 0.0`, `solo = false`.
  3. `Node::Load` reads only the inputs present in the XML. Missing inputs are untouched and preserve their initial defaults.
  4. Project playback, existing audio levels, and track structure are preserved with zero modification.

#### 3.4.3 Forward Compatibility Analysis
- When opening projects containing these new inputs in an older version of Olive:
  `Node::LoadInput` (`app/node/node.cpp:1455-1459`):
  ```cpp
  if (!this->HasInputWithID(param_id)) {
    qWarning() << "Failed to load parameter that didn't exist:" << param_id;
    reader->skipCurrentElement();
    return false;
  }
  ```
  Older Olive binaries will log a non-fatal warning, skip the unrecognized elements, and load the remainder of the timeline seamlessly.

#### 3.4.4 `LoadCustom` / `SaveCustom` Non-Interference
`Track::LoadCustom` and `Track::SaveCustom` handle only timeline row display height (`height` tag). Audio mixing controls are standard node inputs and do not contaminate or modify `LoadCustom`/`SaveCustom`.

---

### 3.5 Undo / Redo Command Integration

#### 3.5.1 Setting Standard Values via `NodeParamSetStandardValueCommand`
In Olive's command subsystem (`app/node/nodeundo.h:739-757`), changing any input standard value with undo/redo is handled by `NodeParamSetStandardValueCommand`:
```cpp
// Pushing a volume adjustment to undo stack
float old_volume = track->GetVolume();
float new_volume = 0.5f;
Core::instance()->undo_stack()->push(
  new NodeParamSetStandardValueCommand(
    NodeKeyframeTrackReference(NodeInput(track, Track::kVolumeInput)),
    new_volume,
    old_volume
  )
);
```

#### 3.5.2 Dragging Sliders in Real-Time via `NodeInputDragger`
When the user drags a fader or pan dial in the upcoming `AudioMixerPanel` (M2):
1. On mouse press: `dragger.Start(ref, rational(0, 1), false)`.
2. On mouse move: `dragger.Drag(new_val)` updates `SetSplitStandardValueOnTrack` live, providing real-time audio playback feedback.
3. On mouse release: `dragger.End(command)` bundles the start and end values into a single undo command pushed to `Core::instance()->undo_stack()`.
4. Pressing Ctrl+Z (Undo) restores `start_value`, triggers `InputValueChangedEvent`, updates the UI strip, and invalidates audio cache.

#### 3.5.3 Solo Button Toggle Command
```cpp
bool old_solo = track->IsSoloed();
bool new_solo = !old_solo;
Core::instance()->undo_stack()->push(
  new NodeParamSetStandardValueCommand(
    NodeKeyframeTrackReference(NodeInput(track, Track::kSoloInput)),
    new_solo,
    old_solo
  )
);
```

---

## 4. Automated Unit Test Blueprint (`tests/timeline/track-audio-tests.cpp`)

A dedicated unit test suite should be added to `tests/timeline/track-audio-tests.cpp` and registered in `tests/timeline/CMakeLists.txt` via `olive_add_test(Timeline TrackAudio track-audio-tests.cpp)`.

### Test Cases Blueprint
```cpp
#include <cmath>
#include "testutil.h"
#include "node/block/clip/clip.h"
#include "node/output/track/track.h"
#include "node/output/track/tracklist.h"
#include "node/project/sequence/sequence.h"

namespace olive {

const int kSampleRate = 48000;
const int kTestDuration = 480; // 10ms

AudioParams CreateTestStereoParams()
{
  return AudioParams(kSampleRate, uint64_t(AV_CH_LAYOUT_STEREO), core::SampleFormat::F32P);
}

// Generate test buffer filled with DC 1.0f on all channels
SampleBuffer CreateDCSampleBuffer(const AudioParams &params, size_t sample_count, float left = 1.0f, float right = 1.0f)
{
  SampleBuffer sb(params, sample_count);
  for (size_t i = 0; i < sample_count; i++) {
    sb.data(0)[i] = left;
    sb.data(1)[i] = right;
  }
  return sb;
}

// 1. Verify volume scaling at unity, half gain (-6 dB), and silence
OLIVE_ADD_TEST(TrackAudioVolumeScaling)
{
  const AudioParams params = CreateTestStereoParams();
  Track track;
  track.set_type(Track::kAudio);

  ClipBlock clip;
  clip.set_length_and_media_out(rational(kTestDuration, kSampleRate));
  track.AppendBlock(&clip);

  SampleBuffer input = CreateDCSampleBuffer(params, kTestDuration, 1.0f, 1.0f);
  NodeValueArray blocks;
  blocks.emplace(track.GetArrayIndexFromBlock(&clip), NodeValue(NodeValue::kSamples, input));

  NodeValueRow row;
  row.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks, &track, true));
  NodeGlobals globals(VideoParams(), params,
                      TimeRange(0, rational(kTestDuration, kSampleRate)),
                      LoopMode::kLoopModeOff);

  // Test Unity Volume (1.0f)
  track.SetVolume(1.0f);
  NodeValueTable table1;
  track.Value(row, globals, &table1);
  SampleBuffer res1 = table1.Get(NodeValue::kSamples).toSamples();
  OLIVE_ASSERT_EQUAL(res1.sample_count(), size_t(kTestDuration));
  for (size_t i = 0; i < size_t(kTestDuration); i++) {
    OLIVE_ASSERT(std::fabs(res1.data(0)[i] - 1.0f) < 1e-6f);
    OLIVE_ASSERT(std::fabs(res1.data(1)[i] - 1.0f) < 1e-6f);
  }

  // Test Half Volume (0.5f)
  track.SetVolume(0.5f);
  NodeValueTable table2;
  track.Value(row, globals, &table2);
  SampleBuffer res2 = table2.Get(NodeValue::kSamples).toSamples();
  for (size_t i = 0; i < size_t(kTestDuration); i++) {
    OLIVE_ASSERT(std::fabs(res2.data(0)[i] - 0.5f) < 1e-6f);
    OLIVE_ASSERT(std::fabs(res2.data(1)[i] - 0.5f) < 1e-6f);
  }

  // Test Silence Volume (0.0f)
  track.SetVolume(0.0f);
  NodeValueTable table3;
  track.Value(row, globals, &table3);
  SampleBuffer res3 = table3.Get(NodeValue::kSamples).toSamples();
  for (size_t i = 0; i < size_t(kTestDuration); i++) {
    OLIVE_ASSERT(std::fabs(res3.data(0)[i] - 0.0f) < 1e-6f);
    OLIVE_ASSERT(std::fabs(res3.data(1)[i] - 0.0f) < 1e-6f);
  }

  OLIVE_TEST_END;
}

// 2. Verify stereo balance panning law (center, hard right, hard left, half pan)
OLIVE_ADD_TEST(TrackAudioStereoPanning)
{
  const AudioParams params = CreateTestStereoParams();
  Track track;
  track.set_type(Track::kAudio);

  ClipBlock clip;
  clip.set_length_and_media_out(rational(kTestDuration, kSampleRate));
  track.AppendBlock(&clip);

  SampleBuffer input = CreateDCSampleBuffer(params, kTestDuration, 1.0f, 1.0f);
  NodeValueArray blocks;
  blocks.emplace(track.GetArrayIndexFromBlock(&clip), NodeValue(NodeValue::kSamples, input));

  NodeValueRow row;
  row.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks, &track, true));
  NodeGlobals globals(VideoParams(), params,
                      TimeRange(0, rational(kTestDuration, kSampleRate)),
                      LoopMode::kLoopModeOff);

  // Hard Right (+1.0): Left channel must be silent (0.0), Right untouched (1.0)
  track.SetPan(1.0f);
  NodeValueTable table_r;
  track.Value(row, globals, &table_r);
  SampleBuffer res_r = table_r.Get(NodeValue::kSamples).toSamples();
  for (size_t i = 0; i < size_t(kTestDuration); i++) {
    OLIVE_ASSERT(std::fabs(res_r.data(0)[i] - 0.0f) < 1e-6f);
    OLIVE_ASSERT(std::fabs(res_r.data(1)[i] - 1.0f) < 1e-6f);
  }

  // Hard Left (-1.0): Left untouched (1.0), Right channel silent (0.0)
  track.SetPan(-1.0f);
  NodeValueTable table_l;
  track.Value(row, globals, &table_l);
  SampleBuffer res_l = table_l.Get(NodeValue::kSamples).toSamples();
  for (size_t i = 0; i < size_t(kTestDuration); i++) {
    OLIVE_ASSERT(std::fabs(res_l.data(0)[i] - 1.0f) < 1e-6f);
    OLIVE_ASSERT(std::fabs(res_l.data(1)[i] - 0.0f) < 1e-6f);
  }

  // Half Right (+0.5): Left attenuated by 50% (0.5), Right untouched (1.0)
  track.SetPan(0.5f);
  NodeValueTable table_hr;
  track.Value(row, globals, &table_hr);
  SampleBuffer res_hr = table_hr.Get(NodeValue::kSamples).toSamples();
  for (size_t i = 0; i < size_t(kTestDuration); i++) {
    OLIVE_ASSERT(std::fabs(res_hr.data(0)[i] - 0.5f) < 1e-6f);
    OLIVE_ASSERT(std::fabs(res_hr.data(1)[i] - 1.0f) < 1e-6f);
  }

  OLIVE_TEST_END;
}

// 3. Verify multi-track solo muting coordination within Sequence
OLIVE_ADD_TEST(TrackAudioSoloMutingCoordination)
{
  Sequence sequence;
  TrackList *audio_tl = sequence.track_list(Track::kAudio);

  Track track1;
  Track track2;
  audio_tl->TrackConnected(&track1, 0);
  audio_tl->TrackConnected(&track2, 1);

  OLIVE_ASSERT_EQUAL(track1.IsEffectivelyMuted(), false);
  OLIVE_ASSERT_EQUAL(track2.IsEffectivelyMuted(), false);

  // Solo Track 1 -> Track 1 active, Track 2 effectively muted
  track1.SetSolo(true);
  OLIVE_ASSERT(audio_tl->HasSoloTrack());
  OLIVE_ASSERT_EQUAL(track1.IsEffectivelyMuted(), false);
  OLIVE_ASSERT_EQUAL(track2.IsEffectivelyMuted(), true);

  // Solo both Track 1 and Track 2 -> both active
  track2.SetSolo(true);
  OLIVE_ASSERT_EQUAL(track1.IsEffectivelyMuted(), false);
  OLIVE_ASSERT_EQUAL(track2.IsEffectivelyMuted(), false);

  // Un-solo Track 1 -> Track 1 effectively muted, Track 2 active
  track1.SetSolo(false);
  OLIVE_ASSERT_EQUAL(track1.IsEffectivelyMuted(), true);
  OLIVE_ASSERT_EQUAL(track2.IsEffectivelyMuted(), false);

  // Un-solo Track 2 -> both active
  track2.SetSolo(false);
  OLIVE_ASSERT(!audio_tl->HasSoloTrack());
  OLIVE_ASSERT_EQUAL(track1.IsEffectivelyMuted(), false);
  OLIVE_ASSERT_EQUAL(track2.IsEffectivelyMuted(), false);

  // Solo Track 1 and also Mute Track 1 -> Track 1 muted by Mute flag, Track 2 muted by Solo logic
  track1.SetSolo(true);
  track1.SetMuted(true);
  OLIVE_ASSERT_EQUAL(track1.IsEffectivelyMuted(), true);
  OLIVE_ASSERT_EQUAL(track2.IsEffectivelyMuted(), true);

  audio_tl->TrackDisconnected(&track1, 0);
  audio_tl->TrackDisconnected(&track2, 1);

  OLIVE_TEST_END;
}

}
```

---

## 5. Summary of Modified & Target Files

| Target File | Scope of Change | Key Changes |
|---|---|---|
| `app/node/output/track/track.h` | Header declarations | Declare `kVolumeInput`, `kPanInput`, `kSoloInput`, `GetVolume()`, `GetPan()`, `IsSoloed()`, `IsEffectivelyMuted()`, `SetVolume()`, `SetPan()`, `SetSolo()`, `VolumeChanged()`, `PanChanged()`, `SoloChanged()`. |
| `app/node/output/track/track.cpp` | Implementation | Define input constants, initialize inputs with flags and slider properties in constructor, implement getters/setters, implement `IsEffectivelyMuted()`, update `GetActiveElementsAtTime` to check `IsEffectivelyMuted()`, apply volume and pan in `ProcessAudioTrack`, handle sibling invalidation in `InputValueChangedEvent`. |
| `app/node/output/track/tracklist.h` | Header declarations | Declare `HasSoloTrack() const` and `TrackSoloChanged(Track*, bool)` signal. |
| `app/node/output/track/tracklist.cpp` | Implementation | Implement `HasSoloTrack()`. |
| `tests/timeline/track-audio-tests.cpp` | New test file | Unit tests verifying volume scaling, pan law, solo coordination, and mute interactions. |
| `tests/timeline/CMakeLists.txt` | Build configuration | Add `olive_add_test(Timeline TrackAudio track-audio-tests.cpp)`. |

This concludes the complete technical exploration and blueprint. All interfaces, algorithms, and compatibility guarantees are verified against the active codebase.
