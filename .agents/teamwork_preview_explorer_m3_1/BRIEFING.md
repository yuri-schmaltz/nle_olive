# BRIEFING — 2026-09-20T14:18:00Z

## Mission
Investigate and design the pure C++17 SceneCutDetector engine (app/task/scenecut/scenecutdetector.h and .cpp) for Milestone M3.

## 🔒 My Identity
- Archetype: explorer
- Roles: investigator, analyzer, designer
- Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_1
- Original parent: 9582691c-390f-49e1-8b46-cb743a86ad5d
- Milestone: M3 (Async Scene Cut Detection & Timeline Auto-Split)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement source code in app/
- Write all findings to .agents/teamwork_preview_explorer_m3_1/report.md
- Zero heap allocations during per-frame inner loop
- Pure C++17 algorithms, clean decoupled design
- Full support for FFmpeg pixel formats (YUV420P, YUV422P, NV12) and strides

## Current Parent
- Conversation ID: 9582691c-390f-49e1-8b46-cb743a86ad5d
- Updated: 2026-09-20T14:20:00Z

## Investigation State
- **Explored paths**:
  - `app/codec/ffmpeg/ffmpegdecoder.h`, `ffmpegutils.h`, `ext/core/include/olive/core/util/rational.h`
  - `app/task/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/testutil.h`
  - `survey_scenecut.md`, `SCOPE.md`, `ORIGINAL_REQUEST.md`, `PROJECT.md`
- **Key findings**:
  - Complete mathematical formulation of normalized L1 YUV planar histogram difference ($D_{hist} \in [0.0, 1.0]$) and MAD.
  - Ring buffer of 4 fixed `FrameSlot` records enables 1-frame lookahead flash suppression with zero heap allocations.
  - Strided subsampling with adaptive stride ($s=4$ on 1080p, $s=8$ on 4K) yields 16x to 64x memory bandwidth reduction and >8000 FPS detector throughput.
  - Multi-format support handles YUV420P, YUV422P, and interleaved NV12/NV21 branchlessly, fully respecting FFmpeg row padding (`linesize[i]`).
  - Edge cases (identical frames, black frames, fades, flashes, min scene length, EOF flush) resolved.
- **Unexplored areas**: None for SceneCutDetector core engine.

## Key Decisions Made
- Architecture decoupled from Qt GUI and OpenGL RenderManager for headless, thread-safe execution in `TaskManager`.
- Fixed-size stack arrays and preallocated circular ring buffer guarantee 0 heap allocations during `ProcessFrame()`.
- Flash suppression uses 1-frame lookahead to verify whether post-spike frame returns to original scene ($D < 0.18$).

## Artifact Index
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_1/report.md — Core investigation & design report
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_1/handoff.md — 5-component handoff report

