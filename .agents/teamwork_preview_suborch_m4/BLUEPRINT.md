# Milestone M4: Implementation Blueprint

## Overview
This blueprint synthesizes the architectural findings and specifications from Explorers M4.1, M4.2, and M4.3 for implementing Final Cut Pro 7 XML and OpenTimelineIO interchange, UI wiring, and automated unit test suites.

---

## 1. File Ownership and Scope Boundaries for Worker
The Worker (`teamwork_preview_worker_m4`) owns and modifies the following files:
1. `app/task/project/fcpxml/loadfcpxml.h` (NEW)
2. `app/task/project/fcpxml/loadfcpxml.cpp` (NEW)
3. `app/task/project/fcpxml/savefcpxml.h` (NEW)
4. `app/task/project/fcpxml/savefcpxml.cpp` (NEW)
5. `app/task/project/fcpxml/CMakeLists.txt` (NEW)
6. `app/task/project/CMakeLists.txt` (MODIFIED: add `add_subdirectory(fcpxml)`)
7. `app/task/project/saveotio/saveotio.cpp` (MODIFIED: fix line 180 transition clobber/leak, remove line 101 heap retainer leak, add markers & speed)
8. `app/task/project/saveotio/saveotio.h` (MODIFIED: add marker serialization helper)
9. `app/task/project/loadotio/loadotio.cpp` (MODIFIED: wrap root in stack retainer, fix error cleanup, add markers & speed deserialization)
10. `app/task/project/loadotio/loadotio.h` (MODIFIED: add marker deserialization helper)
11. `app/window/mainwindow/mainmenu.h` (MODIFIED: add `file_export_fcpxml_item_` and `#ifdef USE_OTIO file_export_otio_item_`)
12. `app/window/mainwindow/mainmenu.cpp` (MODIFIED: instantiate and wire export actions)
13. `app/core.h` (MODIFIED: declare `DialogExportFCPXMLShow()` and `#ifdef USE_OTIO DialogExportOTIOShow()`)
14. `app/core.cpp` (MODIFIED: implement export dialog methods, update `GetProjectFilter`, support `.xml` in open/save)
15. `tests/project/CMakeLists.txt` (MODIFIED: add `fcpxml-tests`, add guarded `otio-tests`)
16. `tests/project/fcpxml-tests.cpp` (NEW: CTest suite)
17. `tests/project/otio-tests.cpp` (NEW: CTest suite)

---

## 2. Component Specifications

### 2.1 Final Cut Pro 7 XML Engine (`app/task/project/fcpxml/`)
- **`SaveFCPXMLTask`**:
  - Inherits `olive::Task`.
  - Streams `<xmeml version="5">` via `QXmlStreamWriter`.
  - Maps frame rates:
    - NTSC (23.976, 29.97, 59.94) -> `<ntsc>TRUE</ntsc>` and integer timebase (24, 30, 60).
    - Non-NTSC (24, 25, 30, 50, 60) -> `<ntsc>FALSE</ntsc>` and integer timebase.
  - Generates `<video><track>...` and `<audio><track>...`.
  - Skips `GapBlock` instances so intervals are implicitly represented by `<start>` and `<end>`.
  - Pre-indexes clip coordinates and serializes reciprocal `<link>` elements for paired audio/video clips.
  - Serializes `<transitionitem>` for `CrossDissolveTransition` with `<alignment>center</alignment>`, `<start>`, `<end>`.
  - Serializes `<marker>` items from `sequence->GetMarkers()` with `<in>`, `<out>`.
- **`LoadFCPXMLTask`**:
  - Inherits `olive::Task`.
  - Streams via `QXmlStreamReader`.
  - Creates `Project`, `Sequence`, `Track` (video and audio).
  - Implicit to explicit gap conversion: inserts `GapBlock` when `clip.start > current_timeline_frame`.
  - Creates `ClipBlock`, sets `in`, `out`, `length`, `media_in`.
  - Creates `TransitionBlock` (`CrossDissolveTransition`), connects `kInBlockInput` and `kOutBlockInput`.
  - Resolves media files via `file://` URLs, creating `Footage` instances.
  - Re-establishes audio/video links using `Node::Link(source, target)` based on `<link>` records.
  - Populates `sequence->GetMarkers()` (`TimelineMarker`).
  - Thread safety: invokes `project_->moveToThread(qApp->thread())` upon completion.
  - On error or cancellation: deletes `project_` and returns false to prevent leaks.

### 2.2 OpenTimelineIO Hardening
- **`saveotio.cpp`**:
  - Line 180: change `otio_block = new OTIO::Transition();` to `otio_block = otio_transition;`.
  - Use `toRationalTime(sequence_rate)` for transition in/out offsets.
  - Remove line 101 heap retainer leak (`new OTIO::Timeline::Retainer<OTIO::Timeline>(otio_timeline)`).
  - Add marker serialization from `sequence->GetMarkers()` to `otio_timeline->markers()`.
  - Add `OTIO::LinearTimeWarp` to `otio_clip->effects()` when speed != 1.0 or reverse == true (`scalar = reverse ? -speed : speed`).
- **`loadotio.cpp`**:
  - Wrap `root` in `OTIO::SerializableObject::Retainer<OTIO::SerializableObjectWithMetadata> root_retainer(root);`.
  - Ensure `delete project_; project_ = nullptr;` on all early error returns.
  - In `LoadMarkers`: deserialize `timeline->markers()` into `sequence->GetMarkers()`.
  - In `BuildBlock`: parse `OTIO::LinearTimeWarp` to restore speed and reverse.
  - Ensure `project_->moveToThread(qApp->thread())` transfers ownership on success.

### 2.3 Main Menu and Core Integration
- **`mainmenu.h` / `mainmenu.cpp`**:
  - Add `QAction* file_export_fcpxml_item_;`
  - `#ifdef USE_OTIO QAction* file_export_otio_item_; #endif`
  - In `CreateFileMenu`: add actions under `file_export_menu_`.
  - Wire actions to `Core::DialogExportFCPXMLShow` and `Core::DialogExportOTIOShow`.
  - Set text and localized labels in `RetranslateUi()`.
- **`core.h` / `core.cpp`**:
  - Implement `DialogExportFCPXMLShow()`: retrieves current sequence via `GetSequenceToExport()`, shows file save dialog with `Final Cut Pro 7 XML (*.xml)`, runs `SaveFCPXMLTask` via `TaskManager`.
  - Implement `#ifdef USE_OTIO DialogExportOTIOShow()`: retrieves current sequence, shows file save dialog with `OpenTimelineIO (*.otio)`, runs `SaveOTIOTask`.
  - In `GetProjectFilter()`: add `{tr("Final Cut Pro 7 XML"), QStringLiteral("xml")}`.
  - In `OpenProjectInternal()`: detect `.xml` and instantiate `LoadFCPXMLTask`.
  - In `SaveProjectInternal()`: detect `.xml` and instantiate `SaveFCPXMLTask`.

### 2.4 Unit Tests & Quality Gate
- **`tests/project/CMakeLists.txt`**:
  - `olive_add_test(Project fcpxml-tests fcpxml-tests.cpp)`
  - `if(OpenTimelineIO_FOUND) olive_add_test(Project otio-tests otio-tests.cpp) endif()`
- **`tests/project/fcpxml-tests.cpp`**:
  - `OLIVE_ADD_TEST(FCPXML_SequenceRoundTrip_ClipsGapsMarkersLinks)`
  - `OLIVE_ADD_TEST(FCPXML_TransitionRoundTrip)`
  - `OLIVE_ADD_TEST(FCPXML_FrameRateMapping)`
  - `OLIVE_ADD_TEST(FCPXML_MalformedXMLErrorHandling)`
- **`tests/project/otio-tests.cpp`**:
  - Wrapped in `#ifdef USE_OTIO` with empty fallback test when not enabled.
  - `OLIVE_ADD_TEST(OTIO_SequenceRoundTrip_TransitionsAndMarkers)`
  - `OLIVE_ADD_TEST(OTIO_SpeedAndReversePreservation)`
- **Quality Gate Criteria**:
  - `cmake --preset linux-asan -B build-linux-asan -DBUILD_GPU_TESTS=OFF`
  - `cmake --build build-linux-asan -j 4`
  - `ctest --test-dir build-linux-asan --output-on-failure`
  - `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`
  - 100% pass, 0 memory leaks, 0 assertion failures.
