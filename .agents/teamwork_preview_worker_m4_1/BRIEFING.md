# BRIEFING — 2026-09-20T18:46:00Z

## Mission
Implement Final Cut Pro 7 XML engine (load/save), harden OpenTimelineIO (fix memory leaks, transition clobber, add markers, clip speed/reverse), wire up main menu & Core export dialogs/open/save, and add comprehensive unit tests under ASan verification.

## 🔒 My Identity
- Archetype: implementer
- Roles: implementer, qa, specialist
- Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_worker_m4_1
- Original parent: 1f1c3fe7-601f-4066-a5db-4f3593b3394d
- Milestone: M4: Editorial Timeline Interchange

## 🔒 Key Constraints
- Exclusive write ownership:
  - app/task/project/fcpxml/loadfcpxml.h (new)
  - app/task/project/fcpxml/loadfcpxml.cpp (new)
  - app/task/project/fcpxml/savefcpxml.h (new)
  - app/task/project/fcpxml/savefcpxml.cpp (new)
  - app/task/project/fcpxml/CMakeLists.txt (new)
  - app/task/project/CMakeLists.txt
  - app/task/project/saveotio/saveotio.h
  - app/task/project/saveotio/saveotio.cpp
  - app/task/project/loadotio/loadotio.h
  - app/task/project/loadotio/loadotio.cpp
  - app/window/mainwindow/mainmenu.h
  - app/window/mainwindow/mainmenu.cpp
  - app/core.h
  - app/core.cpp
  - tests/project/CMakeLists.txt
  - tests/project/fcpxml-tests.cpp (new)
  - tests/project/otio-tests.cpp (new)
- DO NOT cheat. All implementations must be genuine.
- Build command: `cmake --preset linux-asan -B build-linux-asan -DBUILD_GPU_TESTS=OFF && cmake --build build-linux-asan -j 4`
- Gauntlet gate: `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`
- Zero memory leaks, zero assertion failures under AddressSanitizer.

## Current Parent
- Conversation ID: 1f1c3fe7-601f-4066-a5db-4f3593b3394d
- Updated: 2026-09-20T18:46:00Z

## Task Summary
- **What to build**: FCP7 XML import/export task, OTIO memory leak and feature fixes, UI menu items, core integration, unit tests.
- **Success criteria**: 100% test pass on fcpxml-tests and otio-tests, clean gauntlet.py run with AddressSanitizer.
- **Interface contracts**: /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m4/SCOPE.md, BLUEPRINT.md
- **Code layout**: /home/yuri/Documentos/olive/PROJECT.md

## Change Tracker
- **Files modified**: none yet
- **Build status**: not started
- **Pending issues**: none

## Quality Status
- **Build/test result**: pending
- **Lint status**: clean
- **Tests added/modified**: pending

## Loaded Skills
- None

## Key Decisions Made
- [initial decision] Reading required reference files and explorer reports.

## Artifact Index
- /home/yuri/Documentos/olive/.agents/teamwork_preview_worker_m4_1/DISPATCH.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_worker_m4_1/BRIEFING.md
