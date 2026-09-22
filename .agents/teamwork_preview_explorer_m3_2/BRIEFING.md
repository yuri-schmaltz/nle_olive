# BRIEFING — 2026-09-20T14:24:00Z

## Mission
Investigate and design SceneCutTask inheriting olive::Task (app/task/scenecut/scenecuttask.h and .cpp) and its integration into TaskManager with dedicated headless CPU FFmpeg decoding, accurate seeking/timestamp mapping, memory safety / ASan compliance, and queued signal emission.

## 🔒 My Identity
- Archetype: Explorer
- Roles: Investigation, Technical Design, Synthesis
- Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_2
- Original parent: 9582691c-390f-49e1-8b46-cb743a86ad5d
- Milestone: M3 (Async Scene Cut Detection & Timeline Auto-Split)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement in source tree (only in agent directory)
- Must avoid DecoderCache and RenderManager completely for 0 playback contention
- Must answer 6 specific technical questions with complete header/source code architecture ready for worker

## Current Parent
- Conversation ID: 9582691c-390f-49e1-8b46-cb743a86ad5d
- Updated: not yet

## Investigation State
- **Explored paths**:
  - `app/task/task.h`, `app/task/taskmanager.h`, `app/task/taskmanager.cpp`
  - `app/common/cancelableobject.h`, `app/render/cancelatom.h`
  - `app/codec/ffmpeg/ffmpegdecoder.h`, `app/codec/ffmpeg/ffmpegdecoder.cpp`
  - `app/common/ffmpegutils.h`, `ext/core/include/olive/core/util/timecodefunctions.h`, `rational.h`
  - `app/node/block/clip/clip.h`, `clip.cpp`, `app/node/project/footage/footage.h`
  - `app/ai/aiengine.h`, `app/task/precache/`, `app/task/render/`
  - `teamwork_preview_explorer_m3_1/report.md` (SceneCutDetector design)
  - `teamwork_preview_explorer_m3_3/DISPATCH.md` (Timeline integration & tests)
- **Key findings**:
  1. Task lifecycle: `TaskManager::AddTask(Task* t)` takes ownership, runs `QtConcurrent::run(&thread_pool_, &Task::Start, t)`, which invokes `Run()`.
  2. Cancellation: `CancelableObject` thread-safe via `CancelAtom`. Checked via `IsCancelled()`.
  3. Headless CPU decoding: completely decouples from `DecoderCache` and `RenderManager`. Sequential decode at 300-800+ FPS directly accessing YUV planes.
  4. Seeking & Time mapping: `av_seek_frame(..., in_ts, AVSEEK_FLAG_BACKWARD)` plus `avcodec_flush_buffers`. PTS mapped to media time using `Timecode::timestamp_to_time(pts - stream_start_ts, stream->time_base)`. Pre-roll frames prior to `media_in` discarded or used to seed histogram. Decode stops when `pts >= media_out`.
  5. ASan memory safety: RAII `FFmpegSession` ensures `avformat_close_input`, `avcodec_free_context`, `av_packet_free`, `av_frame_free`, `av_dict_free` are called on every exit path.
  6. Signal emission: `SceneCutsDetected(QVector<rational>)` connects across threads with Qt::QueuedConnection.
- **Unexplored areas**: None. Task complete.

## Key Decisions Made
- `SceneCutTask` provides two constructors: one taking `ClipBlock* clip` (extracting parameters safely on caller thread) and one taking explicit media parameters for headless testing.
- RAII session struct `FFmpegSession` for 100% ASan leak prevention.
- Progress updates throttled to avoid event loop flooding.

## Artifact Index
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_2/report.md — Complete investigation & design report with drop-in code
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_2/handoff.md — 5-component handoff report
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_2/progress.md — Liveness & status tracking
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_2/DISPATCH.md — Dispatch log
