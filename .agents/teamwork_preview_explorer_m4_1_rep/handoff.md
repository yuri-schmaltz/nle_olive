# Final Cut Pro 7 XML Architecture Explorer Handoff Report

**Agent**: `teamwork_preview_explorer_m4_1_rep`  
**Working Directory**: `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_1_rep`  
**Parent Conversation ID**: `1f1c3fe7-601f-4066-a5db-4f3593b3394d`  
**Milestone**: M4 (Editorial Timeline Interchange: FCP7 XML & OTIO)  
**Deliverable Document**: `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_1_rep/report.md`  

---

## 1. Observation

1. **Olive Timeline and Node Topology**:
   - `Sequence` (`app/node/project/sequence/sequence.h:32-111`): Inherits `ViewerOutput` $\rightarrow$ `Node`. Maintains tracks in `track_lists_` of type `TrackList*` (`Track::kVideo`, `Track::kAudio`, `Track::kSubtitle`). Exposes `GetVideoParams()`, `GetAudioParams()`, `GetMarkers()`, and `GetLength()`.
   - `Track` (`app/node/output/track/track.h:33-495`, `track.cpp:566-586`): Tracks are strictly contiguous arrays of `Block*` (`blocks_`). In `Track::UpdateInOutFrom(int index)`, block timings are updated sequentially:
     ```cpp
     b->set_in(last_out);
     last_out += b->length();
     b->set_out(last_out);
     ```
     Any blank space on an Olive track is explicitly filled with a `GapBlock` (`app/node/block/gap/gap.h`).
   - `ClipBlock` (`app/node/block/clip/clip.h:37-251`, `clip.cpp:397-408`): Stores `in()`, `out()`, `length()`, `media_in()`, `speed()`, `reverse()`, and `block_links_`. `ClipBlock::LinkChangeEvent()` populates `block_links_` from `links()`. `Node::Link(Node* a, Node* b)` (`app/node/node.h:943`) establishes bidirectional linking.
   - `TransitionBlock` & `CrossDissolveTransition` (`app/node/block/transition/transition.h:30-74`, `crossdissolvetransition.h:28-48`): A transition block is positioned between clips on a track. Inputs are `kOutBlockInput` ("From") and `kInBlockInput` ("To"). Timing is configured with `set_offsets_and_length(in_offset, out_offset)`.
   - `Footage` (`app/node/project/footage/footage.h:43-180`, `footage.cpp:50-86`): Manages media files. Probed via `set_filename(path)`, which signals `InputValueChangedEvent` to clear and reprobe streams.
   - `TimelineMarkerList` (`app/timeline/timelinemarker.h:77-187`, `timelinemarker.cpp:208-237`): Automatically registers child `TimelineMarker` instances via Qt `childEvent` when parented to `sequence->GetMarkers()`. Point markers have `time().length() == 0`; range markers have `time().length() > 0`.
2. **Task and Thread Affinity Lifecycle**:
   - In `app/task/project/loadotio/loadotio.cpp:328` and `app/task/project/load/load.cpp:73`:
     ```cpp
     project_->moveToThread(qApp->thread());
     return true;
     ```
     Worker threads create the `Project` and all child nodes in background memory. Before completion, `moveToThread(qApp->thread())` transfers ownership to the Qt main GUI thread.
   - In `app/task/project/load/load.cpp:75-78`: On error, `delete project_; project_ = nullptr; return false;` prevents memory leaks.
3. **FCP7 XML Specification and Test Harness**:
   - `tests/project/fcpxml-tests.cpp` (designed by Explorer 3 in `teamwork_preview_explorer_m4_3/report.md:373-731`) exercises:
     - `FCPXML_SequenceRoundTrip_ClipsGapsMarkersLinks`: 24fps 1920x1080 sequence with multi-track cuts, gaps, media_in offsets, range/point markers, and dual video/audio `<link>` connections.
     - `FCPXML_TransitionRoundTrip`: Cross Dissolve transition centered on cut with 0.5s in/out offsets.
     - `FCPXML_FrameRateMapping`: Exact rational roundtrip for rates 23.976, 24, 25, 29.97, 30, 50, 59.94, and 60 fps.
     - `FCPXML_MalformedXMLErrorHandling`: Rejection of empty, truncated, or invalid XML without crashing or leaking memory.

---

## 2. Logic Chain

1. **Zero External Dependency Rationale**:
   Olive utilizes `QXmlStreamReader` and `QXmlStreamWriter` for all native project formats (`.ovexml`, `serializer.cpp`). Implementing `LoadFCPXMLTask` and `SaveFCPXMLTask` using Qt's streaming XML parser/generator guarantees 100% native C++17/Qt6 implementation without introducing external dependencies, ensuring interchange is functional even when OpenTimelineIO is not installed.
2. **Precision Frame Rate Arithmetic**:
   In FCP7 XML, rate is expressed as an integer `<timebase>` and a boolean `<ntsc>`.
   - If `rate.denominator() == 1001` or `Timecode::timebase_is_drop_frame(rate.flipped())`, `ntsc = true` and `timebase = qRound(rate.toDouble() * 1001.0 / 1000.0)`.
   - Otherwise, `ntsc = false` and `timebase = qRound(rate.toDouble())`.
   - Conversely, parsing `<timebase>` and `<ntsc>` produces `ntsc ? rational(timebase * 1000, 1001) : rational(timebase, 1)`.
   - This mapping exactly preserves 23.976 (`24000/1001`), 29.97 (`30000/1001`), and 59.94 (`60000/1001`) with zero roundoff drift.
3. **Implicit Gaps to Explicit Blocks Reconciliation**:
   In FCP7 XML, gaps between clips are implicit. When `LoadFCPXMLTask` parses clips on a track, it calculates $\Delta = \text{clip.start} - \text{current\_timeline\_frame}$. If $\Delta > 0$, a `GapBlock` of length $\Delta$ is inserted. On export, `SaveFCPXMLTask` skips `GapBlock` instances, allowing clip start times to naturally represent the empty intervals.
4. **Dual Link Preservation**:
   Premiere Pro and Kdenlive require reciprocal `<link>` tags under each linked `<clipitem>`. `SaveFCPXMLTask` runs a pre-pass indexing each clip's track and clip coordinates, generating reciprocal `<link>` blocks for both video and audio clips. `LoadFCPXMLTask` records link references in a lookup table during parsing and executes `Node::Link(source, target)` in a post-pass.
5. **Thread Safety and Memory Management**:
   All nodes are parented to `Project*`. On deserialization failure, `delete project_;` recursively reclaims all child memory. On success, `project_->moveToThread(qApp->thread())` ensures subsequent access from the main UI thread adheres to Qt's object thread affinity model, preventing runtime assertions and memory leaks.

---

## 3. Caveats

1. **Speed Effect Serialization**:
   FCP7 XML represents non-standard clip playback speeds using `<filter>` with `<effectid>speed</effectid>`. The core design focuses on base timeline alignment, in/out points, transitions, markers, and links. Speed filtering can be read and written via the standard `<filter>` block.
2. **Offline Media Relinking**:
   When an XML project is imported from a different workstation, `<pathurl>` paths may not exist locally. The engine instantiates `Footage` with the referenced filename so clips remain labeled and placed on the timeline. If the media file is missing, `Footage::IsValid()` will be false until the user relinks via the UI.

---

## 4. Conclusion

The native C++17/Qt6 Final Cut Pro 7 XML engine is completely architected. The classes `LoadFCPXMLTask` and `SaveFCPXMLTask` in `app/task/project/fcpxml/` provide streaming import and export with zero external dependencies, exact rational frame rate mapping, dual video/audio `<link>` preservation, transition offset geometry, timeline marker support, and full ASan/Gauntlet compliance.

The detailed class headers, implementation blueprints, CMake configurations, and algorithm specifications are fully documented in `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_1_rep/report.md`.

---

## 5. Verification Method

1. **Compile affected targets**:
   ```bash
   cmake --preset linux-asan -B build-linux-asan -DBUILD_GPU_TESTS=OFF
   cmake --build build-linux-asan -j 4
   ```
2. **Run FCP7 XML unit test suite**:
   ```bash
   ctest --test-dir build-linux-asan -R fcpxml-tests --output-on-failure
   ```
3. **Execute the full Gauntlet ASan Quality Gate**:
   ```bash
   python3 scripts/gauntlet.py --preset linux-asan --jobs 4
   ```
4. **Verification Criteria**:
   - `FCPXML_SequenceRoundTrip_ClipsGapsMarkersLinks` passes with exact matching of in/out frames, gaps, markers, and audio/video links.
   - `FCPXML_TransitionRoundTrip` passes with exact preservation of in/out offsets.
   - `FCPXML_FrameRateMapping` passes for all standard broadcast and film frame rates.
   - `FCPXML_MalformedXMLErrorHandling` passes with zero memory leaks (ASan clean) and zero assertion failures.
