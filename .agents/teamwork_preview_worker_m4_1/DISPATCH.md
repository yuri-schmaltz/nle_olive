## 2026-09-20T18:45:54Z
You are the Implementation Worker for Milestone M4: Editorial Timeline Interchange (teamwork_preview_worker_m4_1).
Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_worker_m4_1
Parent conversation ID: 1f1c3fe7-601f-4066-a5db-4f3593b3394d

MANDATORY FIRST STEP: Read /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.
Also read:
- /home/yuri/Documentos/olive/PROJECT.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m4/SCOPE.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m4/BLUEPRINT.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_1_rep/report.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_2_rep/report.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_3/report.md

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Your Exclusive Write Ownership:
You own and modify ONLY these files:
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

Detailed Objectives:
1. Final Cut Pro 7 XML Engine (app/task/project/fcpxml/):
   - Implement `LoadFCPXMLTask` and `SaveFCPXMLTask` in native C++17/Qt6 using `QXmlStreamReader` and `QXmlStreamWriter`.
   - Exact timebase mapping: convert rational frame rate to integer `<timebase>` and `<ntsc>` flag (e.g. 23.976 -> 24 NTSC, 29.97 -> 30 NTSC, 59.94 -> 60 NTSC, 24 -> 24 non-NTSC, 25 -> 25 non-NTSC, etc.).
   - Support sequences, video and audio tracks, clips (in, out, start, end, media_in, duration), implicit/explicit gaps, transitions (`CrossDissolveTransition`), markers (`sequence->GetMarkers()`), and dual `<link>` tags linking video and audio clipitems.
   - Thread safety: transfer `project_->moveToThread(qApp->thread())` upon completion; delete `project_` on failure.
   - Add `fcpxml/` to `app/task/project/CMakeLists.txt`.
2. OpenTimelineIO Hardening:
   - Fix transition clobber and leak in `app/task/project/saveotio/saveotio.cpp:180` (`otio_block = otio_transition;` using `toRationalTime(sequence_rate)`).
   - Remove leaked heap retainer `timeline_retainer` on line 101 of `saveotio.cpp`.
   - Add bidirectional marker serialization/deserialization in `saveotio.cpp` and `loadotio.cpp` (`sequence->GetMarkers()` <-> `timeline->markers()`).
   - Add clip speed and reverse handling via `OTIO::LinearTimeWarp`.
   - Ensure RAII memory management in `loadotio.cpp` with stack `root_retainer` and `project_->moveToThread(qApp->thread())`.
3. Main Menu Integration:
   - In `app/window/mainwindow/mainmenu.h` and `mainmenu.cpp`: add `file_export_fcpxml_item_` and `#ifdef USE_OTIO file_export_otio_item_` under `file_export_menu_`.
   - Connect to `Core::DialogExportFCPXMLShow` and `Core::DialogExportOTIOShow`.
   - In `app/core.h` and `core.cpp`: implement `DialogExportFCPXMLShow()` and `#ifdef USE_OTIO DialogExportOTIOShow()`, update `GetProjectFilter()` to include `Final Cut Pro 7 XML (*.xml)`, and handle `.xml` in `OpenProjectInternal()` and `SaveProjectInternal()`.
4. Automated Unit Tests:
   - Create `tests/project/fcpxml-tests.cpp` with tests for full sequence round-trip (clips, gaps, markers, links), transitions, frame rate timebase mapping, and malformed XML error handling.
   - Create `tests/project/otio-tests.cpp` with tests for sequence round-trip with transitions and markers under `#ifdef USE_OTIO`.
   - Register tests in `tests/project/CMakeLists.txt` via `olive_add_test` (with `if(OpenTimelineIO_FOUND)` guard for `otio-tests`).
5. Verification:
   - Build using `cmake --preset linux-asan -B build-linux-asan -DBUILD_GPU_TESTS=OFF && cmake --build build-linux-asan -j 4`.
   - Run tests: `ctest --test-dir build-linux-asan -R fcpxml-tests --output-on-failure`.
   - Run full Gauntlet gate: `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`.
   - Ensure 100% pass, zero memory leaks, and zero assertion failures under AddressSanitizer.
6. Deliverables:
   - Write a complete handoff report to `/home/yuri/Documentos/olive/.agents/teamwork_preview_worker_m4_1/handoff.md` with:
     - Exact list of files created/modified
     - Build commands and compilation output
     - Test commands and ctest execution output
     - Gauntlet ASan run results
   - Notify parent via `send_message` to 1f1c3fe7-601f-4066-a5db-4f3593b3394d.
