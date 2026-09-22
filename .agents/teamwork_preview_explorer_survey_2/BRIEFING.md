# BRIEFING — 2026-09-20T14:12:00Z

## Mission
Conduct an in-depth code survey of the Olive Video Editor codebase focusing on Requirement R2 (TaskManager, Video Decoding/Frame Extraction, Scene Cut Detection Algorithms, Timeline Clip Model & Auto-Split, Thread Safety, Unit Testing) and produce survey_scenecut.md and handoff.md.

## 🔒 My Identity
- Archetype: explorer
- Roles: Video Analysis & TaskManager Explorer
- Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_2
- Original parent: 2abb8c8e-0fc7-4809-9b75-0af6692b6370
- Milestone: survey

## 🔒 Key Constraints
- Read-only investigation — do NOT implement
- Base code on C++17 native and Qt6 pure
- Strict DAG node architecture preservation and thread safety of playback/render
- Memory safety (0 memory leaks with ASan)
- Write output reports to designated directory only

## Current Parent
- Conversation ID: 2abb8c8e-0fc7-4809-9b75-0af6692b6370
- Updated: not yet

## Investigation State
- **Explored paths**:
  - `app/task/task.h`, `app/task/taskmanager.{h,cpp}`, `app/common/cancelableobject.h`, `app/widget/taskview/`
  - `app/codec/decoder.{h,cpp}`, `app/codec/ffmpeg/ffmpegdecoder.{h,cpp}`, `app/render/rendercache.h`, `app/render/rendermanager.{h,cpp}`
  - `app/timeline/timelineundosplit.{h,cpp}`, `app/node/block/clip/clip.{h,cpp}`, `app/node/output/track/track.{h,cpp}`
  - `tests/timeline/timeline-tests.cpp`, `tests/testutil.h`, `tests/CMakeLists.txt`, `scripts/gauntlet.py`
  - `app/ai/aiengine.h`
- **Key findings**:
  - `TaskManager` manages `Task` subclasses via `QThreadPool` and `QtConcurrent::run`.
  - Non-interfering decoding requires dedicated headless CPU FFmpeg decoding, avoiding GPU/RenderManager lock and readback bottlenecks.
  - `BlockSplitPreservingLinksCommand` already supports multi-point chronological splitting with automatic audio/video link preservation and full Undo/Redo.
  - Native C++17 YUV strided histogram + MAD algorithm enables 300-800+ FPS cut detection with 0 allocations in the inner loop.
  - All DAG mutations must occur on the main GUI thread upon receiving cut timestamps via Qt queued signals.
- **Unexplored areas**: None for survey scope.

## Key Decisions Made
- Recommended dedicated CPU FFmpeg decoding session over `RenderTask` to prevent playback stutter and achieve maximum throughput.
- Recommended utilizing existing `BlockSplitPreservingLinksCommand` directly for auto-splitting.

## Artifact Index
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_2/survey_scenecut.md — Comprehensive R2 survey report
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_2/handoff.md — 5-component handoff report
