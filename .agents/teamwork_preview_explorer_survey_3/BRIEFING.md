# BRIEFING — 2026-09-20T14:12:00Z

## Mission
Conduct an in-depth code survey of Olive Video Editor focusing on R3 (Timeline Interchange: FCPXML & OTIO), R4 (Linux Packaging: AppImage & Flatpak), and Test/Quality Infrastructure (Gauntlet ASan), documenting findings in survey_interchange_packaging.md.

## 🔒 My Identity
- Archetype: explorer
- Roles: Interchange Packaging Explorer
- Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3
- Original parent: 2abb8c8e-0fc7-4809-9b75-0af6692b6370
- Milestone: survey

## 🔒 Key Constraints
- Read-only investigation — do NOT implement
- 100% Native C++17 and pure Qt6 compatibility
- Must pass Gauntlet with ASan (0 leaks, 0 assertion failures)
- Write only to own folder /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3

## Current Parent
- Conversation ID: 2abb8c8e-0fc7-4809-9b75-0af6692b6370
- Updated: 2026-09-20T14:12:00Z

## Investigation State
- **Explored paths**: `app/node/project/serializer/`, `app/task/project/`, `app/timeline/`, `app/packaging/linux/`, `scripts/gauntlet.py`, `CMakePresets.json`, `cmake/Sanitizers.cmake`, `tests/`
- **Key findings**:
  1. Olive native projects are `.ove` (zlib + OVEC header) and `.ovexml` (uncompressed XML) using QXmlStreamReader/Writer.
  2. OTIO support exists in `loadotio/` and `saveotio/`, but has a critical leak bug on line 180 of `saveotio.cpp` (overwriting populated transition with blank `new OTIO::Transition()`), drops all markers, and lacks timeline-level export UI.
  3. FCP7 XML (`xmeml`) does not exist yet, but can be cleanly implemented in 100% native C++17/Qt6 using `QXmlStreamReader`/`QXmlStreamWriter` with zero extra dependencies.
  4. Packaging: `AppRun` is deficient (missing `LD_LIBRARY_PATH` and `QT_PLUGIN_PATH`), legacy docker script used Qt5 `linuxdeployqt`; created complete modern Qt6 AppImage recipe and Flatpak manifest using `org.kde.Platform` 6.x.
  5. Gauntlet runner: `python3 scripts/gauntlet.py --preset linux-asan --jobs 4` verified empirical 100% pass across all 7 test executables in 6.3s with 0 leaks and 0 assertion errors.
- **Unexplored areas**: None within assigned scope (R3, R4, Gauntlet).

## Key Decisions Made
- Structured complete technical survey in `survey_interchange_packaging.md`.
- Produced comprehensive handoff in `handoff.md`.

## Artifact Index
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3/survey_interchange_packaging.md — Comprehensive survey report
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3/handoff.md — Handoff report
