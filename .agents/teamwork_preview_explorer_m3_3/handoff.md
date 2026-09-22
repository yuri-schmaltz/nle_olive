# 5-Component Handoff Report: Timeline Auto-Split & Unit Tests (Milestone M3)

**Author**: Explorer 3 (`teamwork_preview_explorer_m3_3`)  
**Parent Orchestrator**: `teamwork_preview_suborch_m3` (`9582691c-390f-49e1-8b46-cb743a86ad5d`)  
**Date**: September 20, 2026  
**Type**: Hard Handoff (Investigation & Test Architecture Complete)

---

## 1. Observation

1. **Splitting Command Flaw in Multi-Point Scenarios**:
   In `app/timeline/timelineundosplit.cpp` lines 121–138:
   ```cpp
   for (int i=0;i<times_.size();i++) {
     const rational& time = times_.at(i);
     QVector<Block*> splits(blocks_.size());
     for (int j=0;j<blocks_.size();j++) {
       Block* b = blocks_.at(j);
       if (b->in() < time && b->out() > time) {
         BlockSplitCommand* split_command = new BlockSplitCommand(b, time);
         split_command->redo_now();
         splits.replace(j, split_command->new_block());
         commands_.append(split_command);
       } else {
         splits.replace(j, nullptr);
       }
     }
     splits_.replace(i, splits);
   }
   ```
   Direct observation: In loop `i`, `b` is fetched via `blocks_.at(j)`. When `time_0` splits `b`, `b`'s length is reduced to `time_0 - b->in()`. On iteration `i = 1`, `b` remains the truncated head block. Its `out()` is now `time_0 < time_1`, so `b->out() > time` evaluates to `false`. Therefore, all cut timestamps beyond the first are silently ignored.
2. **Media to Sequence Time Transformation**:
   In `app/node/block/clip/clip.cpp` lines 201–224:
   ```cpp
   rational ClipBlock::MediaToSequenceTime(const rational &media_time) const
   {
     if (media_time == RATIONAL_MIN || media_time == RATIONAL_MAX) return media_time;
     rational sequence_time = media_time - media_in();
     double speed_value = speed();
     if (qIsNull(speed_value)) {
       sequence_time = rational::NaN;
     } else if (!qFuzzyCompare(speed_value, 1.0)) {
       sequence_time = rational::fromDouble(sequence_time.toDouble() / speed_value);
     }
     if (reverse()) {
       sequence_time = length() - sequence_time;
     }
     return sequence_time;
   }
   ```
   Direct observation: `MediaToSequenceTime` maps source media timestamp to clip-local offset `[0, length()]`. The absolute sequence time is $T_{seq} = \text{clip->in()} + \text{clip->MediaToSequenceTime}(T_{media})$.
3. **Menu and Action Wiring Pattern**:
   In `app/widget/menu/menushared.cpp` lines 50–51, 149–150, 338–345:
   `edit_split_item_` and `edit_speedduration_item_` are created via `Menu::CreateItem` and added to `AddItemsForEditMenu(Menu *m, bool for_clips)`. `SpeedDurationTriggered()` calls `timeline->ShowSpeedDurationDialogForSelectedClips()`.
4. **Timeline Context Menu**:
   In `app/widget/timelinewidget/timelinewidget.cpp` lines 1242–1246:
   `MenuShared::instance()->AddItemsForEditMenu(&menu, true);` is called whenever `!selected.isEmpty()`.
5. **Existing Test Framework**:
   In `tests/CMakeLists.txt` lines 17–84:
   Macro `olive_add_test(GROUP NAME SOURCE)` parses `OLIVE_ADD_TEST(...)`, creates the test executable linking `$<TARGET_OBJECTS:libolive-editor>`, and registers it with CTest. `tests/timeline/CMakeLists.txt` currently has tests `timeline-tests` and `tempo-tests`. There is currently no `tests/task` directory.

---

## 2. Logic Chain

1. **From Observation 1 to Command Bug Resolution**:
   - Because `BlockSplitPreservingLinksCommand` was previously only invoked with 1 timestamp (in `timelineundogeneral.cpp:326`, `multicamwidget.cpp:136`, `timelinewidget.cpp:454`, and `tool/razor.cpp:96`), the single-split bug was never triggered in production.
   - When Scene Cut Detection introduces a vector of $M$ cut points $\{t_0, t_1, \dots, t_{M-1}\}$, the failure to track `current_blocks[j] = split_command->new_block()` drops cuts $1 \dots M-1$.
   - Replacing `blocks_.at(j)` with a mutable tracking array `current_blocks` where `current_blocks[j] = split_command->new_block()` guarantees that each successive sorted cut divides the remaining tail block.
2. **From Observation 2 to Sample-Accurate Sequence Cuts**:
   - The headless `SceneCutTask` identifies scene cuts at container media timestamps $T_{media}$.
   - Calling `clip->MediaToSequenceTime(T_{media})` handles speed adjustments ($\div \text{speed}$) and reverse playback ($\text{length} - t$).
   - Adding `clip->in()` shifts the relative time into absolute timeline sequence space.
   - Snapping via `Timecode::snap_time_to_timebase(T_seq, timebase)` enforces video frame alignment.
   - Enforcing $\text{clip->in()} < T_{snapped} < \text{clip->out()}$ prevents assertions and errors in `BlockSplitCommand::redo()`.
3. **From Observation 3 & 4 to Timeline UI Architecture**:
   - Defining `edit_detect_scenes_item_` in `MenuShared` and exposing it in `AddItemsForEditMenu` guarantees that "Auto-Split Scenes..." appears in the main menu Edit bar, in keyboard shortcut settings, and in the right-click context menu of selected timeline clips.
   - `SceneCutDialog` cleanly prompts for sensitivity, min frames, subsampling stride, strobe rejection, and linked audio splitting.
4. **From Observation 5 to Automated Unit Testing Architecture**:
   - Creating `tests/task/CMakeLists.txt` and `tests/task/scenecut-tests.cpp` provides isolated verification of `SceneCutDetector` on synthetic planar YUV buffers without audio/video hardware dependencies.
   - Creating `tests/timeline/scenecut-split-tests.cpp` provides regression tests for multi-point splitting, link preservation, and undo/redo restoration.

---

## 3. Caveats

1. **Intermediate Effects Nodes**:
   If a clip has complex retiming effect nodes inserted between `ClipBlock` and `Footage`, `Node::FindInputNodesConnectedToInput<Footage>` correctly locates the root `Footage`, but complex non-linear time-warping nodes would require sampling the time remapping curve. For M3, standard clip speed and reverse playback are supported.
2. **Asynchronous Clip Lifetime**:
   While `SceneCutTask` is running in `TaskManager` on a background thread, the user may delete the clip, trim it, or close the sequence. The UI receiver lambda must guard the operation using `QPointer<ClipBlock>` and verify `clip && clip->track() && clip->project()` before pushing commands.
3. **Audio-Only Clips**:
   Scene cut detection operates on visual video streams. If a user selects an audio-only clip, `SceneCutDialog` or the action handler should ignore it or notify the user that only video clips can be visually analyzed.

---

## 4. Conclusion

1. **UI Integration**:
   - Add `edit_detect_scenes_item_` to `MenuShared` and `TimelineWidget::ShowContextMenu`.
   - Implement `SceneCutDialog` (`app/dialog/scenecut/`) for user parameter acquisition.
2. **Timeline Splitting Fix**:
   - Fix `BlockSplitPreservingLinksCommand::prepare()` in `app/timeline/timelineundosplit.cpp` by maintaining `current_blocks[j]` across sequential cut points.
3. **Thread-Safe Command Execution**:
   - Receive cuts via queued Qt connection on the main thread, map via $T_{seq} = \text{clip->in()} + \text{clip->MediaToSequenceTime}(T_{media})$, snap to timebase, filter boundaries, and push `BlockSplitPreservingLinksCommand` to `Core::instance()->undo_stack()`.
4. **Automated Unit Testing**:
   - Deploy `tests/task/scenecut-tests.cpp` (8 synthetic computer vision test cases) via `olive_add_test(Task scenecut-tests ...)`.
   - Deploy `tests/timeline/scenecut-split-tests.cpp` (5 editorial multi-cut, link preservation, and undo/redo test cases) via `olive_add_test(Timeline scenecut-split-tests ...)`.
   - Connect both in CMake build system (`tests/CMakeLists.txt` and `tests/timeline/CMakeLists.txt`).

---

## 5. Verification Method

1. **Inspecting Reports and Blueprints**:
   - View `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_3/report.md` for complete class headers, source codes, and test suite blueprints.
2. **Verifying the Splitting Bug Fix**:
   - Inspect `app/timeline/timelineundosplit.cpp` lines 112–140. Verify that `blocks_.at(j)` was not updating for subsequent `i` iterations and that `current_blocks[j]` resolves this issue.
3. **Compiling & Running Unit Tests**:
   - Run:
     ```bash
     cmake -B build -S .
     cmake --build build --target scenecut-tests scenecut-split-tests -j4
     ctest --test-dir build -R "scenecut" --output-on-failure
     ```
4. **Gauntlet ASan Verification**:
   - Run:
     ```bash
     python3 scripts/gauntlet.py --preset linux-asan --jobs 4
     ```
   - Invalidation condition: Any heap leak reported by AddressSanitizer or assertion failure in `BlockSplitCommand::redo()`.
