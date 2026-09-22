## 2026-09-20T14:16:14Z

You are the Sub-Orchestrator for Milestone M4: Editorial Timeline Interchange - FCPXML & OTIO (teamwork_preview_suborch_m4).
Your working directory is: /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m4

MANDATORY FIRST STEP: Read the authoritative user request at /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.
Also read /home/yuri/Documentos/olive/PROJECT.md, /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m4/SCOPE.md, and the survey at /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3/survey_interchange_packaging.md.

Your parent is teamwork_preview_orchestrator_1 (conversation ID: 2abb8c8e-0fc7-4809-9b75-0af6692b6370).

Your objective:
Orchestrate the delivery of Milestone M4:
1. Final Cut Pro 7 XML (xmeml):
   - Native C++17/Qt6 `LoadFCPXMLTask` and `SaveFCPXMLTask` in `app/task/project/fcpxml/` using `QXmlStreamReader`/`Writer`.
   - Lossless interchange of sequences, video/audio tracks, clips, in/out points, transitions, markers, and `<link>` dual audio/video references.
   - Exact timebase mapping and thread-safe project handoff (`project->moveToThread(qApp->thread())`).
2. OpenTimelineIO Hardening:
   - Fix transition clobber and leak in `app/task/project/saveotio/saveotio.cpp:180`.
   - Add marker serialization and deserialization in `saveotio.cpp` and `loadotio.cpp`.
3. Main Menu Integration:
   - Add export actions for FCP7 XML and OTIO in `MainMenu::file_export_menu_`.
4. Unit Tests & Quality Gate:
   - Implement `tests/project/fcpxml-tests.cpp` and `tests/project/otio-tests.cpp` in `tests/CMakeLists.txt`.
   - Verify 100% pass under ASan (`ctest --test-dir build-linux-asan` and `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`) with 0 leaks and 0 assertion errors.

Run the standard Explorer -> Worker -> Reviewer -> Challenger -> Auditor -> Gate cycle.
When finished, send your completion report via send_message to parent (conversation ID: 2abb8c8e-0fc7-4809-9b75-0af6692b6370).
