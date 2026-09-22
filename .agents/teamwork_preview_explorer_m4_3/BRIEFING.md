# BRIEFING — 2026-09-20T14:24:00Z

## Mission
Investigate UI wiring, CMake configuration, and unit test architecture for Milestone M4 (FCP7 XML & OTIO interchange).

## 🔒 My Identity
- Archetype: explorer
- Roles: investigator, reporter
- Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_3
- Original parent: 1f1c3fe7-601f-4066-a5db-4f3593b3394d
- Milestone: M4

## 🔒 Key Constraints
- Read-only investigation — do NOT implement
- Scope: UI wiring in mainmenu/core, CMake targets in app and tests, unit test suite design (fcpxml-tests.cpp, otio-tests.cpp)
- Strict adherence to Olive codebase patterns and ASan safety requirements

## Current Parent
- Conversation ID: 1f1c3fe7-601f-4066-a5db-4f3593b3394d
- Updated: not yet

## Investigation State
- **Explored paths**:
  - `app/window/mainwindow/mainmenu.h`, `mainmenu.cpp`
  - `app/core.h`, `app/core.cpp`
  - `app/task/project/CMakeLists.txt`, `app/task/project/saveotio/`, `app/task/project/loadotio/`
  - `app/node/project/sequence/`, `app/node/output/track/`, `app/node/block/`
  - `app/timeline/timelinemarker.h`, `timelinemarker.cpp`
  - `tests/CMakeLists.txt`, `tests/testutil.h`, `tests/project/CMakeLists.txt`, `tests/project/project-tests.cpp`
  - `scripts/gauntlet.py`, `CMakePresets.json`
- **Key findings**:
  - Main menu export actions designed for FCP7 XML and OTIO (`#ifdef USE_OTIO`).
  - Core slots `DialogExportFCPXMLShow()` and `DialogExportOTIOShow()` designed using `GetSequenceToExport()` and `TaskDialog`.
  - Project filters updated in `Core::GetProjectFilter()` and handlers added in `OpenProjectInternal()` and `SaveProjectInternal()`.
  - CMake targets mapped cleanly: `app/task/project/fcpxml/` in `app/task/project/CMakeLists.txt`; `fcpxml-tests` and `otio-tests` in `tests/project/CMakeLists.txt` (with `if(OpenTimelineIO_FOUND)` guard).
  - Complete, robust unit test blueprints designed for `tests/project/fcpxml-tests.cpp` and `tests/project/otio-tests.cpp` with zero-leak RAII lifecycle and ASan compliance.
- **Unexplored areas**: None within M4 UI, CMake, and Test scope.

## Key Decisions Made
- `tests/project/fcpxml-tests.cpp` is registered unconditionally as FCPXML is pure Qt6 / C++17 with zero external dependencies.
- `tests/project/otio-tests.cpp` is conditionally compiled via `if(OpenTimelineIO_FOUND)` in CMake and `#ifdef USE_OTIO` in C++.
- Synchronous task execution (`task.Start()`) used in unit tests to ensure direct assertion of return codes and errors without thread flakiness.

## Artifact Index
- `DISPATCH.md` — record of dispatch messages
- `BRIEFING.md` — persistent working memory
- `progress.md` — liveness heartbeat and task progress
- `report.md` — detailed architecture and unit test design report
- `handoff.md` — 5-component self-contained handoff report
