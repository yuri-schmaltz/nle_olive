## 2026-09-20T14:17:53Z

You are the Interchange UI and Tests Explorer (teamwork_preview_explorer_m4_3).
Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_3
Parent conversation ID: 1f1c3fe7-601f-4066-a5db-4f3593b3394d

MANDATORY FIRST STEP: Read /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.
Also read:
- /home/yuri/Documentos/olive/PROJECT.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m4/SCOPE.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3/survey_interchange_packaging.md

Your Objective:
Investigate UI wiring, CMake configuration, and unit test architecture for Milestone M4:
1. Main Menu Export Actions:
   - Examine `app/window/mainwindow/mainmenu.h` and `mainmenu.cpp`.
   - Locate `file_export_menu_` and inspect how `file_export_media_item_` is set up.
   - Design menu items: `Export -> Final Cut Pro 7 XML (*.xml)` and `Export -> OpenTimelineIO (*.otio)` (wrapped in `#ifdef USE_OTIO`).
   - Examine `app/core.h` and `core.cpp` for corresponding slot/methods (`DialogExportFCPXMLShow()`, `DialogExportOTIOShow()`), project filter additions in `GetProjectFilter()`, and file dialog handling.
2. CMake Integration:
   - Check `app/CMakeLists.txt` for adding `app/task/project/fcpxml/` source files.
   - Check `tests/CMakeLists.txt` for how tests are registered using `olive_add_test`.
3. Unit Test Suite Design:
   - Design `tests/project/fcpxml-tests.cpp`:
     - Test sequence creation with video and audio tracks, clips, gaps, transitions, markers.
     - Save to FCP7 XML using `SaveFCPXMLTask`.
     - Load from FCP7 XML using `LoadFCPXMLTask`.
     - Assert exact match on sequence frame rate, track counts, clip in/out/start/end/media_in, transitions, markers, and audio/video link preservation.
     - Test error handling on malformed XML.
   - Design `tests/project/otio-tests.cpp`:
     - Test sequence roundtrip with transitions and markers under OTIO.
     - Verify transition is preserved without memory leak or clobber.
   - Verify that test cases run cleanly with AddressSanitizer (0 leaks, 0 assertions) under `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`.
4. Deliverables:
   - Write a detailed analysis and test design report to `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_3/report.md`.
   - Write a self-contained handoff to `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_3/handoff.md`.
   - Notify parent via `send_message` to 1f1c3fe7-601f-4066-a5db-4f3593b3394d.
