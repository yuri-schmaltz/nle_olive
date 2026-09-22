# Handoff Report: Survey of Timeline Interchange (FCPXML/OTIO), Linux Packaging Automation, and Quality Gate Infrastructure

**Agent**: `teamwork_preview_explorer_survey_3` (Interchange Packaging Explorer)  
**Handoff Type**: Hard Handoff (Task complete)  
**Date**: 2026-09-20  

---

## 1. Observation

1. **Native Project Persistence & Formats**:
   - `app/node/project/serializer/serializer.cpp:76-88`: Compressed projects start with magic 4-byte header `OVEC` and are inflated via `qUncompress`.
   - `app/node/project/serializer/serializer.cpp:180-213`: `ProjectSerializer::Save` serializes to XML via `QXmlStreamWriter` with root tag `<olive version="YYMMDD" url="...">`. Writes are committed atomically using `QSaveFile` with fallback disabled (`setDirectWriteFallback(false)`).
   - `app/core.cpp:1099-1130`: `Core::GetProjectFilter` defines `.ove` (Olive Project), `.ovexml` (Uncompressed XML), and optionally `.otio` (OpenTimelineIO).

2. **Existing OpenTimelineIO (OTIO) Implementation**:
   - `app/task/project/loadotio/loadotio.cpp:57-90`: Parses OTIO JSON files using `OTIO::SerializableObjectWithMetadata::from_json_file`. Iterates over `tracks()` and adds video/audio tracks.
   - `app/task/project/saveotio/saveotio.cpp:172-181`: In `SaveOTIOTask::SerializeTrack`, lines 173-181 contain:
     ```cpp
     } else if (dynamic_cast<TransitionBlock*>(block)) {
       auto otio_transition = new OTIO::Transition(block->GetLabel().toStdString());
       TransitionBlock* our_transition = static_cast<TransitionBlock*>(block);
       otio_transition->set_in_offset(our_transition->in_offset().toRationalTime());
       otio_transition->set_out_offset(our_transition->out_offset().toRationalTime());
       otio_block = new OTIO::Transition();
     }
     ```
     This allocates `otio_transition`, configures its offsets, and then immediately overwrites `otio_block` with an empty `new OTIO::Transition()`, leaking `otio_transition` and losing all transition data.
   - Neither `loadotio.cpp` nor `saveotio.cpp` reads or writes `sequence->GetMarkers()`.
   - OTIO is conditionally compiled via `#ifdef USE_OTIO` in `CMakeLists.txt:203-211`.
   - In `app/window/mainwindow/mainmenu.cpp:62-63`, `file_export_menu_` contains only `file_export_media_item_`. There are no menu actions for exporting OTIO or FCPXML.

3. **Final Cut Pro 7 XML (`xmeml`) Status**:
   - Ripgrep search across the entire codebase confirmed that no FCP7 XML / xmeml parser or serializer exists.
   - Olive already has full Qt XML stream utilities (`QXmlStreamReader`, `QXmlStreamWriter`, and helper functions in `app/common/xmlutils.h`), enabling a native C++17/Qt6 implementation without third-party dependencies.

4. **Timeline Elements & Data Structures**:
   - `Sequence` (`app/node/project/sequence/sequence.h`) inherits `ViewerOutput` (`app/node/output/viewer/viewer.h`), storing `TimelineMarkerList* markers_`, `VideoParams`, and `track_lists_`.
   - `Track` (`app/node/output/track/track.h`) contains ordered `QVector<Block*> blocks_`.
   - `ClipBlock` (`app/node/block/clip/clip.h`) inherits `Block` (`app/node/block/block.h`), exposing `in()`, `out()`, `length()`, `media_in()`, `speed()`, `reverse()`, and `block_links()`.
   - `Timecode` (`ext/core/include/olive/core/util/timecodefunctions.h`) provides `time_to_timestamp()`, `timestamp_to_time()`, and `timebase_is_drop_frame()`.

5. **Existing Packaging Infrastructure**:
   - `app/packaging/linux/AppRun`:
     ```bash
     #!/usr/bin/env bash
     APPDIR=$(readlink -f $(dirname "$0"))
     "$APPDIR/usr/bin/olive-editor" "$@"
     ```
     Lacks configuration for `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, and `XDG_DATA_DIRS`.
   - `docker/scripts/build_olive.sh`: Legacy script relying on Qt5 and `linuxdeployqt`.
   - `app/packaging/linux/CMakeLists.txt`: Successfully installs `.desktop`, `.appdata.xml`, MIME type definition, and icon hierarchy (16x16 to 512x512).

6. **Gauntlet & Quality Gate Infrastructure**:
   - `scripts/gauntlet.py`: Isolates environment (`XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME` in `qa-results/<stamp>/`), runs git checks, presets configure (`cmake --preset linux-asan -B build-linux-asan -DBUILD_GPU_TESTS=OFF`), builds (`cmake --build build-linux-asan -j 4`), and executes ctest (`--output-on-failure --no-tests=error --timeout 120 --output-junit`).
   - Tool execution of `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`:
     Passed with exit code 0; all 7 test executables passed in 6.329s (`qa-results/20260920T141009.116996Z/report.json`).

---

## 2. Logic Chain

1. **Interchange Fidelity (R3)**:
   - Olive's core object model (`Sequence`, `Track`, `ClipBlock`, `GapBlock`, `TransitionBlock`, `TimelineMarker`) directly maps to FCP7 XML tags (`<sequence>`, `<track>`, `<clipitem>`, `<transitionitem>`, `<marker>`).
   - Frame and rate precision is maintained by mapping Olive's `rational` to FCP7 XML's integer `<timebase>` and boolean `<ntsc>` flag (e.g. 24000/1001 -> timebase 24, ntsc TRUE).
   - Because FCP7 XML is parsed via `QXmlStreamReader` and serialized via `QXmlStreamWriter`, it can be implemented as `LoadFCPXMLTask` and `SaveFCPXMLTask` in `app/task/project/` with 0 external dependencies, ensuring it is always available even when OTIO is disabled.
   - High fidelity with Kdenlive and Premiere Pro requires:
     - Exact `<start>`, `<end>`, `<in>`, `<out>` frame calculation.
     - Dual `<link>` elements linking video and audio clipitems.
     - Percent-encoded `file:///` URIs with relative-path fallback for media relinking.
   - For OTIO, fixing line 180 of `saveotio.cpp` and adding marker/speed serialization restores functional parity.

2. **Packaging Automation (R4)**:
   - `linuxdeployqt` cannot be used for Qt6 without error. Modern Qt6 AppImage builds require either `linuxdeploy` with Qt6 plugins or a self-contained bundling script copying Qt6 platform plugins (`platforms/libqxcb.so`) and shared libraries into `AppDir/usr/lib`, paired with an `AppRun` that sets `LD_LIBRARY_PATH` and `QT_PLUGIN_PATH`.
   - Flatpak requires the `org.kde.Platform` / `org.kde.Sdk` 6.x runtime to provide the native Qt6 environment, alongside modules for Imath, OpenEXR, OpenColorIO, OpenImageIO, PortAudio, and Olive.

3. **Gauntlet & Sanitizer Compliance**:
   - `scripts/gauntlet.py` executes under strict AddressSanitizer and UndefinedBehaviorSanitizer flags (`-fsanitize=address,undefined -fno-omit-frame-pointer`).
   - To maintain 0 leaks and 0 assertion errors in new interchange tasks:
     - Tasks must transfer created `Project` / `Sequence` objects across threads safely using `project_->moveToThread(qApp->thread());`.
     - All temporary XML / OTIO heap allocations must be managed using RAII / `std::unique_ptr` / Qt object trees.

---

## 3. Caveats

- OpenTimelineIO C++ library in `vendor-libs/` is currently built for Linux CentOS/VFX Platform 2022; host builds without system OTIO packages will run with OTIO disabled unless OTIO is installed or vendored. However, native FCP7 XML support will work on all platforms regardless of OTIO presence.
- GPU-dependent tests require an active display/OpenGL context; `--preset linux-asan` in `gauntlet.py` explicitly runs with `-DBUILD_GPU_TESTS=OFF` by default unless `--gpu` is specified.

---

## 4. Conclusion

1. **R3 (Timeline Interchange)**:
   - A complete architectural blueprint has been documented in `survey_interchange_packaging.md`.
   - FCP7 XML (`xmeml`) can be cleanly implemented in `app/task/project/fcpxml/` using pure Qt6 `QXmlStreamReader`/`Writer`.
   - Existing OTIO bugs (transition leak/clobber in `saveotio.cpp:180`, missing markers) must be fixed.
   - Dedicated "Export -> Final Cut Pro 7 XML (*.xml)" and "Export -> OpenTimelineIO (*.otio)" actions must be added to `MainMenu`.
2. **R4 (Linux Packaging)**:
   - AppImage recipe and portable `AppRun` script have been specified to replace obsolete Qt5 deployment scripts.
   - A complete Flatpak manifest (`org.olivevideoeditor.Olive.json`) targeting KDE Runtime 6.8+ has been structured.
3. **Quality Gate Compliance**:
   - `scripts/gauntlet.py --preset linux-asan --jobs 4` currently passes 100% (7/7 test executables in 6.3s).
   - New unit tests for FCPXML and OTIO roundtrips must be integrated into `tests/project/CMakeLists.txt` to guarantee continued 0-leak / 0-assertion conformance.

---

## 5. Verification Method

To independently verify the findings of this survey:

1. **Inspect Survey Report**:
   ```bash
   cat /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3/survey_interchange_packaging.md
   ```
2. **Run Gauntlet Quality Gate**:
   ```bash
   python3 /home/yuri/Documentos/olive/scripts/gauntlet.py --preset linux-asan --jobs 4
   ```
   *Expected result*: Exit code 0, status "passed", 7/7 test executables passed, report generated in `qa-results/<stamp>/report.json`.
3. **Inspect Existing OTIO Transition Bug**:
   ```bash
   sed -n '170,185p' /home/yuri/Documentos/olive/app/task/project/saveotio/saveotio.cpp
   ```
4. **Inspect Existing AppRun Script**:
   ```bash
   cat /home/yuri/Documentos/olive/app/packaging/linux/AppRun
   ```
