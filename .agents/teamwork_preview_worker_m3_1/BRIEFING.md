# BRIEFING — 2026-09-20T14:24:00Z

## Mission
Implement Async Scene Cut Detection & Timeline Auto-Split for Milestone M3.

## 🔒 My Identity
- Archetype: implementer
- Roles: implementer, qa, specialist
- Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_worker_m3_1
- Original parent: 9582691c-390f-49e1-8b46-cb743a86ad5d
- Milestone: M3

## 🔒 Key Constraints
- Exclusive write ownership:
  - app/task/CMakeLists.txt
  - app/task/scenecut/CMakeLists.txt
  - app/task/scenecut/scenecutdetector.h
  - app/task/scenecut/scenecutdetector.cpp
  - app/task/scenecut/scenecuttask.h
  - app/task/scenecut/scenecuttask.cpp
  - app/dialog/scenecut/scenecutdialog.h
  - app/dialog/scenecut/scenecutdialog.cpp
  - app/dialog/scenecut/CMakeLists.txt
  - app/widget/menu/menushared.h
  - app/widget/menu/menushared.cpp
  - app/widget/timelinewidget/timelinewidget.cpp
  - app/panel/timeline/timeline.h
  - app/panel/timeline/timeline.cpp
  - app/timeline/timelineundosplit.cpp
  - tests/CMakeLists.txt
  - tests/task/CMakeLists.txt
  - tests/task/scenecut-tests.cpp
  - tests/timeline/CMakeLists.txt
  - tests/timeline/scenecut-split-tests.cpp
- Zero memory leaks (ASan compliance, 0 leaks, 0 errors).
- Genuine implementations only (no hardcoding, no facades).
- Python scripts/gauntlet.py --preset linux-asan --jobs 4 must achieve 100% pass.

## Current Parent
- Conversation ID: 9582691c-390f-49e1-8b46-cb743a86ad5d
- Updated: 2026-09-20T14:24:00Z

## Task Summary
- **What to build**: Pure C++17 SceneCutDetector, SceneCutTask (Task-based FFmpeg decoding & detection), multi-point cut fix in BlockSplitPreservingLinksCommand::prepare(), UI wiring in MenuShared/TimelineWidget/TimelinePanel, unit tests in tests/task and tests/timeline.
- **Success criteria**: 100% test pass on gauntlet linux-asan, 0 leaks, 0 regressions.
- **Interface contracts**: PROJECT.md, SCOPE.md, SYNTHESIS.md
- **Code layout**: PROJECT.md

## Key Decisions Made
- [Initial turn setup]

## Change Tracker
- **Files modified**: None yet
- **Build status**: Untested
- **Pending issues**: None

## Quality Status
- **Build/test result**: Untested
- **Lint status**: Untested
- **Tests added/modified**: None

## Loaded Skills
- None
