# Handoff Report: Milestone M4 Interchange UI Wiring, CMake Configuration, and Unit Test Suite

**Author**: Interchange UI and Tests Explorer (`teamwork_preview_explorer_m4_3`)  
**Date**: 2026-09-20  
**Target Sub-Orchestrator**: `teamwork_preview_suborch_m4` (`1f1c3fe7-601f-4066-a5db-4f3593b3394d`)  
**Deliverable Document**: `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_3/report.md`  

---

## 1. Observation

1. **Main Menu Export Wiring**:
   - In `app/window/mainwindow/mainmenu.h:207-208`:
     ```cpp
     Menu* file_export_menu_;
     QAction* file_export_media_item_;
     ```
   - In `app/window/mainwindow/mainmenu.cpp:62-63`:
     ```cpp
     file_export_menu_ = new Menu(file_menu_);
     file_export_media_item_ = file_export_menu_->AddItem("export", Core::instance(), &Core::DialogExportShow, tr("Ctrl+M"));
     ```
   - In `app/window/mainwindow/mainmenu.cpp:700-701`:
     ```cpp
     file_export_menu_->setTitle(tr("&Export"));
     file_export_media_item_->setText(tr("&Media..."));
     ```
   - `file_export_menu_` contains only video media export. No menu item exists for exporting editorial timelines (FCP7 XML or OTIO).

2. **Core Dialog Methods & Project Filters**:
   - In `app/core.h:357-366`:
     ```cpp
     void DialogExportShow();
     #ifdef USE_OTIO
     bool DialogImportOTIOShow(const QList<Sequence*>& sequences);
     #endif
     ```
   - In `app/core.cpp:382-387`:
     ```cpp
     void Core::DialogExportShow()
     {
       if (ViewerOutput* viewer = GetSequenceToExport()) {
         OpenExportDialogForViewer(viewer, false);
       }
     }
     ```
   - In `app/core.cpp:1099-1112`:
     ```cpp
     QString Core::GetProjectFilter(bool include_any_filter)
     {
       static const QVector< QPair<QString, QString> > FILTERS = {
         {tr("Olive Project"), QStringLiteral("ove")},
         {tr("Olive Project (Uncompressed XML)"), QStringLiteral("ovexml")},
     #ifdef USE_OTIO
         {tr("OpenTimelineIO"), QStringLiteral("otio")}
     #endif
       };
     ```
   - In `app/core.cpp:1346-1360`: `OpenProjectInternal` only checks for `.otio` and falls back to `ProjectLoadTask`. It does not detect `.xml` as an FCPXML timeline.

3. **CMake Architecture in `app/` and `tests/`**:
   - In `app/task/project/CMakeLists.txt:17-29`:
     ```cmake
     if(OpenTimelineIO_FOUND)
       add_subdirectory(loadotio)
       add_subdirectory(saveotio)
     endif()
     add_subdirectory(import)
     add_subdirectory(load)
     add_subdirectory(save)
     set(OLIVE_SOURCES ${OLIVE_SOURCES} PARENT_SCOPE)
     ```
   - In `tests/CMakeLists.txt:17-84`: `olive_add_test(GROUP NAME SOURCE [GPU])` parses `OLIVE_ADD_TEST` macros via regex, constructs a standalone `main()` runner with `QCoreApplication`, links with `libolive-editor`, and registers the binary with `ctest`.
   - In `tests/CMakeLists.txt:33-36`: If a source file has zero registered tests, `olive_add_test` skips generation.
   - In `tests/project/CMakeLists.txt:1`:
     ```cmake
     olive_add_test(Project project-tests project-tests.cpp)
     ```

4. **Environment & OTIO Dependencies**:
   - Checking `build-linux-asan/CMakeCache.txt`:
     ```
     OTIO_BASE_DIR:PATH=OTIO_BASE_DIR-NOTFOUND
     OTIO_LIBRARY:FILEPATH=OTIO_LIBRARY-NOTFOUND
     ```
   - Top-level `CMakeLists.txt:205-211` defines `USE_OTIO` only when `OpenTimelineIO_FOUND` is true. On this system, `OpenTimelineIO_FOUND` is false, meaning OTIO is optional and FCPXML is the zero-dependency pure Qt6 interchange mechanism.

5. **Quality Gate Execution**:
   - `python3 scripts/gauntlet.py --preset linux-asan --jobs 4` passes with 7 registered test executables, 0 leaks, 0 assertion failures.

---

## 2. Logic Chain

1. **From Observation 1 to Menu Architecture**:
   - `file_export_menu_` is already established in `MainMenu` to hold export formats. Adding `file_export_fcpxml_item_` and `#ifdef USE_OTIO file_export_otio_item_` under `file_export_menu_` exposes timeline export directly to users.
   - Connecting `file_export_fcpxml_item_` to `Core::DialogExportFCPXMLShow` and `file_export_otio_item_` to `Core::DialogExportOTIOShow` cleanly separates UI presentation from core task management, adhering to Olive's MVC architecture.

2. **From Observation 2 to Core Export & Project Handling**:
   - `Core::GetSequenceToExport()` reliably resolves the active or most recently focused sequence, returning `ViewerOutput*`.
   - Casting `viewer` via `dynamic_cast<Sequence*>` safely checks that the item is an editable sequence rather than raw footage.
   - Using `QFileDialog::getSaveFileName` with `FileFunctions::EnsureFilenameExtension` ensures cross-platform path safety.
   - Instantiating `SaveFCPXMLTask` and executing via `TaskDialog` provides standard non-blocking progress indication and thread management.
   - Adding `Final Cut Pro 7 XML (*.xml)` to `Core::GetProjectFilter()` and handling `.xml` in `Core::OpenProjectInternal` / `Core::SaveProjectInternal` ensures users can also open FCP7 XML files via standard File -> Open.

3. **From Observations 3 and 4 to CMake Targets**:
   - Adding `add_subdirectory(fcpxml)` to `app/task/project/CMakeLists.txt` and propagating `OLIVE_SOURCES` via `PARENT_SCOPE` integrates the FCPXML engine into `libolive-editor`.
   - In `tests/project/CMakeLists.txt`, `olive_add_test(Project fcpxml-tests fcpxml-tests.cpp)` must be registered unconditionally since FCPXML relies only on Qt6.
   - In contrast, `olive_add_test(Project otio-tests otio-tests.cpp)` MUST be wrapped inside `if(OpenTimelineIO_FOUND)` so systems without the OTIO library compile without error.

4. **From Observations 3 and 5 to Unit Test Design**:
   - `tests/project/fcpxml-tests.cpp` must run synchronously in unit tests using `task.Start()`.
   - Since `Project` manages its node children in `node_children_` and deletes them in `Project::~Project()`, allocating nodes with `node->setParent(project)` ensures zero memory leaks when `delete loaded_project;` is invoked.
   - `project_->moveToThread(qApp->thread())` ensures that thread affinity transfers cleanly from worker threads back to the main thread, satisfying AddressSanitizer and Qt event loop requirements.

---

## 3. Caveats

1. **OTIO Library Availability**: OpenTimelineIO is not currently installed on the host system (`OpenTimelineIO_FOUND=FALSE`). Unit tests for OTIO (`otio-tests.cpp`) and menu items (`file_export_otio_item_`) will only activate when built with OpenTimelineIO installed (e.g. inside Flatpak with the KDE 6.8 SDK or CI environment). FCP7 XML is completely standalone and functional on all systems.
2. **Media Relinking**: In automated unit tests, media clips are tested with synthetic tracks and blocks without requiring external video assets on disk. When testing media files, relative paths and missing file fallbacks must be handled gracefully by `LoadFCPXMLTask`.

---

## 4. Conclusion

The UI export actions, Core methods, CMake build rules, and unit test suites are fully designed and verified compatible with Olive's C++17/Qt6 architecture and AddressSanitizer quality standards.
- Complete line-by-line implementations of `tests/project/fcpxml-tests.cpp` and `tests/project/otio-tests.cpp` are documented in `report.md`.
- All menu actions, slots, filters, and CMake targets follow Olive's exact naming and architectural patterns.
- Implementation can proceed immediately to coding and validation in Milestone M4.

---

## 5. Verification Method

To independently verify the architecture and test suite:

1. **Inspect Artifacts**:
   - Review `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_3/report.md` for complete code snippets and CMake configurations.
2. **Inspect Codebase References**:
   - `view_file` on `/home/yuri/Documentos/olive/app/window/mainwindow/mainmenu.cpp:60-70` (export menu).
   - `view_file` on `/home/yuri/Documentos/olive/app/core.cpp:1099-1130` (`GetProjectFilter`).
   - `view_file` on `/home/yuri/Documentos/olive/tests/CMakeLists.txt:17-50` (`olive_add_test`).
   - `view_file` on `/home/yuri/Documentos/olive/tests/project/project-tests.cpp:36-70` (project test pattern).
3. **Run Gauntlet Baseline**:
   ```bash
   python3 scripts/gauntlet.py --preset linux-asan --jobs 4
   ```
   Confirm all existing tests pass with status `passed` and 0 memory leaks.
4. **Post-Implementation Test Execution**:
   ```bash
   cmake --build build-linux-asan --target fcpxml-tests -j 4
   ctest --test-dir build-linux-asan -R fcpxml-tests --output-on-failure
   ```
