# Handoff Report: Track Audio Mixing Controls (Volume, Pan, Solo)

**From**: `explorer_track_audio` (Teamwork Explorer)  
**To**: `teamwork_preview_suborch_m1` (Milestone Sub-Orchestrator)  
**Date**: September 20, 2026  
**Artifact**: `/home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_track_audio/report.md`  

---

## 1. Observation

1. **Existing Track Inputs**:
   In `app/node/output/track/track.cpp:40-58`:
   ```cpp
   const QString Track::kBlockInput = QStringLiteral("block_in");
   const QString Track::kMutedInput = QStringLiteral("muted_in");
   const QString Track::kArrayMapInput = QStringLiteral("arraymap_in");

   Track::Track() : ...
   {
     AddInput(kBlockInput, NodeValue::kNone, InputFlags(kInputFlagArray | kInputFlagNotKeyframable | kInputFlagHidden | kInputFlagIgnoreInvalidations));
     AddInput(kMutedInput, NodeValue::kBoolean, false, InputFlags(kInputFlagNotConnectable | kInputFlagNotKeyframable));
     AddInput(kArrayMapInput, NodeValue::kBinary, InputFlags(kInputFlagStatic | kInputFlagHidden | kInputFlagIgnoreInvalidations));
   ```
   Track only possesses mute and lock. Volume, pan, and solo inputs are absent.

2. **Track Audio Processing Loop**:
   In `app/node/output/track/track.cpp:628-726`:
   `Track::ProcessAudioTrack` constructs `SampleBuffer block_range_buffer(globals.aparams(), range.length())`, silences it, copies active clip samples into it, and directly pushes `block_range_buffer` into the output table:
   ```cpp
   table->Push(NodeValue::kSamples, QVariant::fromValue(block_range_buffer), this);
   ```
   No volume attenuation or stereo pan balance transformation is applied to this buffer.

3. **Active Elements & Muting**:
   In `app/node/output/track/track.cpp:102-107`:
   ```cpp
   Node::ActiveElements Track::GetActiveElementsAtTime(const QString &input, const TimeRange &r) const
   {
     if (input == kBlockInput) {
       if (IsMuted() || blocks_.empty() || r.in() >= track_length() || r.out() <= 0) {
         return ActiveElements::kNoElements;
       }
   ```
   When `IsMuted()` is true, `ActiveElements::kNoElements` is returned, causing `NodeTraverser` to bypass evaluating blocks entirely.

4. **Multi-Track Graph Connection & TrackList Relationship**:
   In `app/timeline/timelineundogeneral.cpp:107-112`:
   Audio tracks are merged into a binary tree using `MathNode` (addition) connected to `Sequence::kSamplesInput`.
   In `app/node/output/track/tracklist.cpp:89-91`:
   ```cpp
   track->set_type(type_);
   track->set_sequence(parent());
   ```
   Every `Track` in a sequence holds a pointer `sequence_` to its owning `Sequence`.

5. **Legacy Unit Tests Evaluation**:
   In `tests/timeline/tempo-tests.cpp:195-204`:
   ```cpp
   NodeValueRow row;
   row.insert(Track::kBlockInput, NodeValue(NodeValue::kSamples, blocks, &track, true));
   ...
   track.Value(row, globals, &table);
   ```
   Tests instantiate standalone `Track`s (`sequence_ == nullptr`) and pass `row` without mixing inputs. If `ProcessAudioTrack` blindly evaluates `value[kVolumeInput].toDouble()` without a fallback check, an invalid `NodeValue` returns `0.0`, silencing the output and failing the test suite.

6. **SIMD Buffer Manipulation**:
   In `ext/core/src/render/samplebuffer.cpp:149-175`:
   `SampleBuffer::transform_volume(float)` and `SampleBuffer::transform_volume_for_channel(int, float)` provide SSE/NEON-vectorized in-place scaling.

7. **XML Serialization & Deserialization**:
   In `app/node/node.cpp:1335-1341` and `1455-1459`:
   `Node::Save` automatically saves all declared inputs. `Node::Load` skips missing inputs when reading XML, preserving the defaults set in the constructor (`vol=1.0`, `pan=0.0`, `solo=false`).

---

## 2. Logic Chain

1. **Input Declaration**:
   By declaring `kVolumeInput` ("volume_in", float, 1.0, dB view), `kPanInput` ("pan_in", float, 0.0, range -1.0 to 1.0, percentage view), and `kSoloInput` ("solo_in", bool, false) in `Track::Track()` with `kInputFlagNotConnectable | kInputFlagNotKeyframable`, Track gains native mixer properties exposed to `NodeParamView`, `AudioMixerPanel` (M2), and the DAG engine.
2. **Safe Parameter Fetching**:
   By using `value.contains(kVolumeInput) && value[kVolumeInput].isValid() ? value[kVolumeInput].toDouble() : GetStandardValue(kVolumeInput).toFloat()`, volume and pan are accurately evaluated both during full DAG traversal (`NodeTraverser`) and when invoked in standalone unit tests (`tempo-tests.cpp`).
3. **In-Place DSP Execution**:
   In `Track::ProcessAudioTrack`, executing `block_range_buffer.transform_volume(vol)` and `block_range_buffer.transform_volume_for_channel(channel, factor)` applies volume and stereo panning in-place with zero memory allocation, zero locks, and full SIMD acceleration.
4. **Solo Muting Coordination**:
   Let $H = \bigvee_{i \in \text{Tracks}} S_i$. A track is effectively muted if $M_k \lor (H \land \neg S_k)$.
   Implementing `TrackList::HasSoloTrack()` and `Track::IsEffectivelyMuted()` enables checking this state.
   Checking `IsEffectivelyMuted()` in `GetActiveElementsAtTime` bypasses block traversal and decoding. Checking it in `ProcessAudioTrack` guarantees immediate silence even under direct invocation.
5. **Cache Invalidation Topology**:
   When `solo_in` changes, sibling tracks' effective mute states change. Emitting `TrackList::TrackSoloChanged` and iterating sibling audio tracks to call `t->InvalidateCache(TimeRange(0, t->track_length()), kSoloInput)` ensures the timeline and viewer audio caches accurately refresh.
6. **Project Backward/Forward Compatibility**:
   Because inputs are automatically serialized by `Node::SaveInput` and deserialized by `Node::LoadInput`, older projects lacking these inputs default safely to unity gain, center pan, and un-soloed without modifying XML schemas.
7. **Undo/Redo**:
   Using `NodeParamSetStandardValueCommand` and `NodeInputDragger` provides native integration with Olive's undo stack for faders, pan knobs, and solo toggles.

---

## 3. Caveats

1. **Keyframed Automation**:
   The current design specifies `kInputFlagNotKeyframable` for track volume and pan. This matches NLE mixer channel strip semantics (Premiere, Resolve, FCP7). If per-sample keyframed track volume automation is desired in a future milestone, `Track` must implement `ProcessSamples` and resolve sample jobs.
2. **Multichannel / Surround Beyond Stereo**:
   The pan balance law is specifically designed for 2-channel stereo. For mono tracks or future 5.1/7.1 surround layouts, channel mapping matrices would be needed. Currently, if `channel_count() != 2`, panning is bypassed to avoid incorrect downmixing.
3. **Standalone Track Instances in Isolation**:
   When a `Track` is instantiated standalone outside of a `Sequence` (such as in isolated unit tests), `sequence_` is `nullptr`. In this case, `IsEffectivelyMuted()` evaluates solely to `IsMuted()`, allowing isolated tests to run without having to mock a complete sequence.

---

## 4. Conclusion

The technical exploration is complete. The exact blueprint is documented in:
`/home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_track_audio/report.md`

All implementation details, mathematical formulas, header interfaces, XML serialization lifecycles, undo/redo mechanisms, and unit tests (`tests/timeline/track-audio-tests.cpp`) are fully designed and ready for the implementer agent to execute.

---

## 5. Verification Method

1. **Code Inspection**:
   Inspect `report.md` for complete class interfaces and code diff blueprints.
2. **Build and Unit Test Verification (Implementer Step)**:
   Once implemented, compile and run the new test suite:
   ```bash
   ctest --test-dir build-linux-asan -R TrackAudio --output-on-failure
   ctest --test-dir build-linux-asan -R TempoStream --output-on-failure
   ```
3. **Gauntlet ASan Gate**:
   Execute the project-wide ASan gauntlet check:
   ```bash
   python3 scripts/gauntlet.py --preset linux-asan --jobs 4
   ```
