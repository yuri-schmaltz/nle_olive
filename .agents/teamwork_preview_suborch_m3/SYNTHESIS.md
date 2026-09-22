# Synthesis & Implementation Blueprint: Milestone M3

## 1. Executive Summary
Milestone M3 delivers native C++17 Asynchronous Scene Cut Detection & Timeline Auto-Split for Olive Video Editor.
The technical investigations of Explorers 1, 2, and 3 have provided complete, production-ready specifications for:
1. `SceneCutDetector` engine (`app/task/scenecut/scenecutdetector.h`, `.cpp`) with zero heap allocations in inner loop, normalized L1 YUV planar histogram distance, spatial MAD, $O(1)$ rolling adaptive threshold, lookahead flash suppression, and strided subsampling.
2. `SceneCutTask` (`app/task/scenecut/scenecuttask.h`, `.cpp`) running in `TaskManager` with dedicated headless CPU FFmpeg decoding (zero contention with playback/RenderManager), accurate seeking/timestamping, and RAII cleanup under ASan.
3. Fix for multi-point cut bug in `BlockSplitPreservingLinksCommand` (`app/timeline/timelineundosplit.cpp:121-138`) to properly track sequential block splits.
4. UI integration in `MenuShared` and `TimelineWidget` with timeline sequence time conversion and `Core::instance()->undo_stack()` execution.
5. Unit test suites `tests/task/scenecut-tests.cpp` (8 tests) and `tests/timeline/scenecut-split-tests.cpp` (5 tests) integrated into `tests/CMakeLists.txt`.

## 2. File Ownership for Worker
Worker 1 has exclusive write ownership of:
- `app/task/CMakeLists.txt`
- `app/task/scenecut/CMakeLists.txt`
- `app/task/scenecut/scenecutdetector.h`
- `app/task/scenecut/scenecutdetector.cpp`
- `app/task/scenecut/scenecuttask.h`
- `app/task/scenecut/scenecuttask.cpp`
- `app/dialog/scenecut/scenecutdialog.h` (optional/simple dialog or direct trigger)
- `app/dialog/scenecut/scenecutdialog.cpp`
- `app/widget/menu/menushared.h`
- `app/widget/menu/menushared.cpp`
- `app/widget/timelinewidget/timelinewidget.cpp`
- `app/panel/timeline/timeline.h`
- `app/panel/timeline/timeline.cpp`
- `app/timeline/timelineundosplit.cpp`
- `tests/CMakeLists.txt`
- `tests/task/CMakeLists.txt`
- `tests/task/scenecut-tests.cpp`
- `tests/timeline/CMakeLists.txt`
- `tests/timeline/scenecut-split-tests.cpp`

## 3. Detailed Component Architecture

### Component A: `SceneCutDetector` (`app/task/scenecut/`)
- Pure C++17, zero heap allocations in `ProcessFrame()`.
- Planar histograms: 256 bins for Y, U, V (`std::array<uint32_t, 256>`).
- Subsampling stride: $s = 4$ default.
- Distance metric: $D = 0.70 \cdot D_{Y} + 0.15 \cdot D_{U} + 0.15 \cdot D_{V} + 0.10 \cdot D_{MAD}$.
- Rolling adaptive threshold: $\text{thresh} = \max(\theta_{base}, \mu + k_{sigma} \cdot \sigma)$ using $O(1)$ ring buffer with running sum & running sum-of-squares.
- Flash suppression: 4-slot ring buffer. When candidate cut detected at frame $t$, hold in lookahead; if inter-frame distance $D(t-1, t+1) < 0.18$, mark as strobe flash and suppress.
- Flush method on EOF to emit pending candidate if valid.

### Component B: `SceneCutTask` (`app/task/scenecut/`)
- Subclasses `olive::Task`.
- Takes `QString filename`, `int stream_idx`, `rational media_in`, `rational media_out`, `rational frame_rate`, `SceneCutConfig config`.
- In `Run()`:
  - Opens `AVFormatContext` and dedicated `AVCodecContext` with `"threads" = "auto"`.
  - Seeks to `media_in` using `av_seek_frame` with backward flag.
  - Feeds pre-roll frames to `detector` to prime baseline statistics without emitting cuts.
  - Decodes frames sequentially within $[media\_in, media\_out]$.
  - Checks `if (IsCancelled()) return false;` on every packet/frame.
  - Updates progress: `emit ProgressChanged(double fraction);`.
  - On EOF or loop completion, calls `detector.Flush()` and collects cut timestamps.
  - Emits queued signal: `emit SceneCutsDetected(cuts);`.
  - Uses stack RAII `FFmpegSession` to guarantee `avformat_close_input`, `avcodec_free_context`, `av_frame_free`, `av_packet_free` on all return paths (ASan 0-leak compliance).

### Component C: Multi-Point Splitting Bug Fix (`app/timeline/timelineundosplit.cpp`)
- Replace the static lookup `Block* b = blocks_.at(j);` in the loop over `times_` with a mutable `current_blocks[j]` vector.
- When `split_command->redo_now()` completes, set `current_blocks[j] = split_command->new_block()`.
- This ensures subsequent cuts divide the newly formed tail blocks correctly.

### Component D: Timeline UI & Execution Wiring
- In `MenuShared`: Add `edit_detect_scenes_item_` in `AddItemsForEditMenu()`.
- In `TimelinePanel`: Add slot `DetectSceneCutsForSelectedClips()`.
- In `TimelineWidget::ShowContextMenu`: Add "Auto-Split Scenes..." action for selected video clips.
- Upon user trigger:
  - Retrieve selected `ClipBlock*`. Traverse upstream to find `Footage*` via `Node::FindInputNodesConnectedToInput<Footage>`.
  - Retrieve `filename`, `stream_index`, `frame_rate`, and `media_range()`.
  - Create `SceneCutTask* task = new SceneCutTask(...)`.
  - Connect `task->SceneCutsDetected` using `QPointer<ClipBlock> safe_clip`:
    - On main GUI thread: Map media timestamps $T_{media}$ to timeline sequence time:
      $T_{seq} = \text{safe\_clip->in()} + \text{safe\_clip->MediaToSequenceTime}(T_{media})$.
    - Snap to sequence timebase: `Timecode::snap_time_to_timebase(T_seq, timebase)`.
    - Filter boundaries: $\text{safe\_clip->in()} < T_{seq} < \text{safe\_clip->out()}$.
    - Execute `BlockSplitPreservingLinksCommand({safe_clip}, valid_cuts)` via `Core::instance()->undo_stack()->push(...)`.
  - Add task to `TaskManager::instance()->AddTask(task)`.

### Component E: Automated Unit Tests
1. `tests/task/scenecut-tests.cpp`:
   - `SceneCutDetector_IdenticalFrames`: black & white sequences produce score 0.0.
   - `SceneCutDetector_HardCutBlackToWhite`: black to white transition triggers cut.
   - `SceneCutDetector_ChromaShift`: identical luminance with different chroma triggers cut.
   - `SceneCutDetector_FlashSuppression`: 1-frame strobe spike (black -> white -> black) is rejected.
   - `SceneCutDetector_MinSceneDuration`: rapid transitions closer than min frames are rejected.
   - `SceneCutDetector_NV12Stride`: NV12 format with row padding is parsed correctly.
   - `SceneCutDetector_EOFFlush`: cut on the last frame is flushed cleanly.
2. `tests/timeline/scenecut-split-tests.cpp`:
   - `TimelineAutoSplit_MultiCut`: multi-point cut splits 1 block into $N+1$ blocks.
   - `TimelineAutoSplit_UndoRedo`: undo restores original block, redo reapplies all splits.
   - `TimelineAutoSplit_LinkedAudioVideo`: video + audio blocks remain linked across all split segments.
   - `TimelineAutoSplit_BoundaryRejection`: cuts at or outside $[in, out]$ are safely ignored.
   - `TimelineAutoSplit_SpeedAndReverse`: time mapping with 2.0x speed and reverse playback.
