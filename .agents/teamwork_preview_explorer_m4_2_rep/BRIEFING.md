# BRIEFING — 2026-09-20T18:43:16Z

## Mission
Conduct a detailed code investigation of Olive's OpenTimelineIO implementation in `app/task/project/saveotio/` and `app/task/project/loadotio/`, covering transition serialization bug, marker serialization/deserialization, clip speed/reverse effects (LinearTimeWarp), thread safety and ASan compliance.

## 🔒 My Identity
- Archetype: explorer
- Roles: investigation, synthesis
- Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_2_rep
- Original parent: 1f1c3fe7-601f-4066-a5db-4f3593b3394d
- Milestone: milestone_4 (OTIO Hardening)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement directly in source files.
- Produce structured analysis report and self-contained handoff.
- Keep BRIEFING.md updated and progress.md heartbeat current.

## Current Parent
- Conversation ID: 1f1c3fe7-601f-4066-a5db-4f3593b3394d
- Updated: not yet

## Investigation State
- **Explored paths**:
  - `app/task/project/saveotio/saveotio.h`, `saveotio.cpp`
  - `app/task/project/loadotio/loadotio.h`, `loadotio.cpp`
  - `app/task/project/load/load.h`, `load.cpp`
  - `app/timeline/timelinemarker.h`, `timelinemarker.cpp`
  - `app/node/block/clip/clip.h`, `clip.cpp`
  - `app/node/block/transition/transition.h`
  - `app/ui/colorcoding.h`
  - `ext/core/include/olive/core/util/rational.h`, `rational.cpp`
  - `ext/core/include/olive/core/util/timerange.h`
  - `tests/project/otio-tests.cpp` (designed by Explorer 3)
- **Key findings**:
  - `saveotio.cpp:180` overwrites `otio_transition` with `new OTIO::Transition()`, dropping in/out offsets, label, and leaking memory. Fix: `otio_block = otio_transition;`.
  - Additional major leak found in `saveotio.cpp:101`: `new OTIO::Timeline::Retainer<OTIO::Timeline>(otio_timeline)` heap-allocates an unretained Retainer, preventing `possibly_delete()` from freeing the timeline! Fix: remove this bogus line.
  - `SaveOTIOTask` constructor only accepts `Project*`, but callers (UI export dialog and unit tests `tests/project/otio-tests.cpp`) need `SaveOTIOTask(Project* project, const QString& filename = QString())`.
  - Markers: `SaveOTIOTask::SerializeTimeline` serializes `sequence->GetMarkers()` into `otio_timeline->markers()` using `OTIO::Marker` with mapped colors and `toRationalTime(sequence_rate)`. `LoadOTIOTask` deserializes into `TimelineMarker` parented to `sequence->GetMarkers()`.
  - Speed/Reverse: `SaveOTIOTask::SerializeClip` serializes `speed` and `reverse` to `OTIO::LinearTimeWarp` with `time_scalar = reverse ? -speed : speed`. `LoadOTIOTask` reads `ltw->time_scalar()` (`std::abs(scalar)` -> speed, `scalar < 0` -> reverse).
  - ASan & Thread Safety: `LoadOTIOTask::Run()` leaks `root` because `possibly_delete()` was never called. Wrapping in `Retainer<SerializableObjectWithMetadata> root_retainer(root)` ensures automatic deletion across all exit paths. On error/cancel, `project_` is deleted and reset to nullptr. `project_->moveToThread(qApp->thread())` ensures thread affinity matches the main thread.
- **Unexplored areas**: None remaining for this scope.

## Key Decisions Made
- Mapped all 16 Olive `ColorCoding` constants to standard OTIO `Marker::Color` strings and vice versa.
- Confirmed `OTIO::LinearTimeWarp::time_scalar` negative value convention matches both OpenTimelineIO spec and Olive test assertions.
- Defined line-by-line patch blueprint for both `saveotio` and `loadotio`.

## Artifact Index
- DISPATCH.md — Initial dispatch instructions
- BRIEFING.md — Situational awareness
- progress.md — Liveness heartbeat
- report.md — Detailed analysis and line-by-line patch blueprint
- handoff.md — 5-component handoff report
