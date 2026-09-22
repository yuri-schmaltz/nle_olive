# Scope: Milestone M3 (Async Scene Cut Detection & Auto-Split)

## Objectives
Implement native C++17 Asynchronous Scene Cut Detection in `TaskManager` and Timeline Auto-Split with link preservation and undo/redo.

## Requirements & Scope Boundaries
1. **SceneCutDetector Engine**:
   - Create `app/task/scenecut/scenecutdetector.h` and `scenecutdetector.cpp`.
   - Algorithm: YUV planar color histogram difference ($L_1$ norm), Mean Absolute Deviation, rolling adaptive threshold, flash suppression.
   - Zero heap allocations in inner loop; pure C++17.
2. **SceneCutTask in TaskManager**:
   - Create `app/task/scenecut/scenecuttask.h` and `scenecuttask.cpp` inheriting `olive::Task`.
   - Dedicated headless CPU FFmpeg decoding context (`AVFormatContext`, `AVCodecContext`) - does not touch `RenderManager` or `DecoderCache`.
   - Support progress reporting via `ProgressChanged(double)` and atomic cancellation via `CancelAtom`.
3. **Timeline Auto-Split Integration**:
   - UI trigger in timeline / clip context menu ("Detect Scene Cuts...").
   - Convert detected media timestamps to timeline sequence timestamps.
   - Execute `BlockSplitPreservingLinksCommand` on GUI thread to split video and linked audio blocks with full undo/redo.
4. **Automated Unit Tests**:
   - Create `tests/task/scenecut-tests.cpp` validating detector sensitivity, synthetic frame transitions, and cancellation under ASan.
   - Create `tests/timeline/scenecut-split-tests.cpp` validating multi-point cut execution, link preservation, and undo/redo state restoration.
   - Integrate into `tests/CMakeLists.txt` via `olive_add_test`.
   - Must pass `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`.

## Interface Contracts
- See PROJECT.md § Interface Contracts: SceneCutTask ↔ Timeline Auto-Split.
