# Scope: Milestone M4 (Editorial Timeline Interchange: FCPXML & OTIO)

## Objectives
Implement native C++17/Qt6 Final Cut Pro 7 XML (`xmeml`) import/export, fix and harden OpenTimelineIO (`.otio`), and wire export actions into the main menu.

## Requirements & Scope Boundaries
1. **FCP7 XML (`xmeml`) Importer & Exporter**:
   - Create `app/task/project/fcpxml/loadfcpxml.h`, `.cpp` and `savefcpxml.h`, `.cpp` inheriting `olive::Task`.
   - Use Qt `QXmlStreamReader` and `QXmlStreamWriter` with zero external dependencies.
   - Support sequences, video and audio tracks, clipitems, in/out points, transitions, markers, and `<link>` dual video/audio references.
   - Precision timebase mapping: rational frame rate to integer `<timebase>` and `<ntsc>` flag.
   - Safe thread transfer: `project->moveToThread(qApp->thread())`.
2. **OpenTimelineIO Hardening**:
   - Fix transition clobber and memory leak in `app/task/project/saveotio/saveotio.cpp:180`.
   - Add marker serialization and deserialization in `saveotio.cpp` and `loadotio.cpp`.
   - Ensure speed/reverse flags are correctly preserved.
3. **Main Menu Integration**:
   - Add "Export -> Final Cut Pro 7 XML (*.xml)" and "Export -> OpenTimelineIO (*.otio)" to `MainMenu::file_export_menu_` in `app/window/mainwindow/mainmenu.cpp`.
4. **Automated Unit Tests**:
   - Create `tests/project/fcpxml-tests.cpp` validating round-trip XML serialization/deserialization, in/out fidelity, link preservation, and ASan 0-leak compliance.
   - Create `tests/project/otio-tests.cpp` validating OTIO transition and marker preservation.
   - Integrate into `tests/CMakeLists.txt` via `olive_add_test`.
   - Must pass `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`.

## Interface Contracts
- See PROJECT.md § Interface Contracts: FCP7 XML & OTIO ↔ Core Project Model.
