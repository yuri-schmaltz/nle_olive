# Codebase Survey Report: Timeline Interchange (FCPXML & OTIO), Linux Packaging Automation, and Quality Gate Infrastructure

**Date**: 2026-09-20  
**Author**: Interchange Packaging Explorer (`teamwork_preview_explorer_survey_3`)  
**Scope**: Requirements R3 (Editorial Timeline Interchange), R4 (Linux Packaging Automation), and Quality Gate / Gauntlet Infrastructure  
**Target Codebase**: Olive Video Editor (`/home/yuri/Documentos/olive`)  

---

## 1. Executive Summary & Problem Framing

Olive Video Editor is a non-linear video editor built on **C++17** and **Qt6**, utilizing a node-based directed acyclic graph (DAG) architecture for compositing, audio rendering, and video processing. To achieve competitive parity and professional adoption, Olive must support seamless editorial migration between industry-standard NLEs (Adobe Premiere Pro, Apple Final Cut Pro, Kdenlive, and DaVinci Resolve) and provide self-contained, reproducible packaging for Linux distributions.

This investigation provides an exhaustive survey of:
1. **Timeline Interchange (R3)**: Analyzing how Olive handles project serialization, the current state of OpenTimelineIO (OTIO), the design and requirements for native Final Cut Pro 7 XML (`xmeml`) import/export, and in-memory timeline element structures (`Sequence`, `Track`, `ClipBlock`, `GapBlock`, `TransitionBlock`, `Footage`, `TimelineMarker`).
2. **Linux Packaging Automation (R4)**: Evaluating existing packaging files, identifying complete build/runtime dependency trees (Qt6, FFmpeg, OCIO, OIIO, OpenEXR, PortAudio), designing a clean and reproducible AppImage creation recipe, and providing a production-grade Flatpak manifest.
3. **Quality Gate Infrastructure**: Dissecting `scripts/gauntlet.py`, CMake presets, AddressSanitizer/UBSan compiler flags, and ctest test runners, establishing exact criteria to maintain zero memory leaks and zero assertion failures.

---

## 2. Requirement R3: Timeline Interchange (FCPXML & OTIO)

### 2.1 Current Olive Project Persistence Architecture

#### Project File Formats
Olive has two native project file extensions, defined and handled in `app/node/project/serializer/`:
1. **`.ove`**: The default standard Olive project. It is a zlib-compressed XML file prefixed with the 4-byte magic signature `OVEC` (0x4F, 0x56, 0x45, 0x43) followed by Qt's `qCompress` byte payload (`serializer.cpp:76-88, 200`).
2. **`.ovexml`**: An uncompressed plain XML text representation, useful for debugging and version control diffs (`serializer.cpp:822`).

#### Serialization Mechanism
Serialization and deserialization are implemented through an abstract versioned class hierarchy based on Qt's streaming XML parser/generator (`QXmlStreamReader` and `QXmlStreamWriter`):
- Base class: `ProjectSerializer` (`app/node/project/serializer/serializer.h`).
- Concrete versions: `ProjectSerializer210528`, `ProjectSerializer210907`, `ProjectSerializer211228`, `ProjectSerializer220403`, `ProjectSerializer230220`.
- Versioning format: `YYMMDD` integer representation stored in the `<olive version="230220" url="...">` XML root attribute.
- The serializer separates data serialization into:
  - `ProjectSerializer::SaveData`: Contains nodes, layout, markers, keyframes, and properties.
  - `ProjectSerializer::LoadData`: Contains loaded nodes, promised connections, markers, keyframes, and properties.
  - Atomic write protection: Projects are written using `QSaveFile` (`serializer.cpp:192-212`) with `setDirectWriteFallback(false)` to prevent project corruption during power loss, full disks, or crashes.

#### UI and Task Pipeline for Project Loading/Saving
- **Opening a Project**: Handled by `Core::OpenProject` (`app/core.cpp:1316-1371`). It detects whether the file extension is `.otio` or `.ove`/`.ovexml`. If `.otio`, it instantiates `LoadOTIOTask`; otherwise, `ProjectLoadTask`.
- **Saving a Project**: Handled by `Core::SaveProjectInternal` (`app/core.cpp:802-851`). If the filename ends with `.otio`, it creates `SaveOTIOTask`; otherwise, `ProjectSaveTask`.
- **Media Import vs. Timeline Import**:
  - Media files (video, audio, images) are imported via `Core::DialogImportShow` (`app/core.cpp:1373-1430`) -> `ImportTask` (`app/task/project/import/import.cpp`). This probes files with FFmpeg and creates `Footage` items inside project folders.
  - There is currently **no dedicated menu item** in `MainMenu` (`app/window/mainwindow/mainmenu.cpp`) for importing or exporting editorial timelines (FCPXML/OTIO) directly into/from an existing open project; OTIO is only accessible as whole-project open/save.

---

### 2.2 Survey of Existing OTIO and FCP7 XML Support

#### OpenTimelineIO (OTIO) Status
OTIO support is conditionally compiled via `#ifdef USE_OTIO` controlled by `FindOpenTimelineIO.cmake` and `CMakeLists.txt:203-211`.
The implementation resides in:
- `app/task/project/loadotio/loadotio.h`, `loadotio.cpp`
- `app/task/project/saveotio/saveotio.h`, `saveotio.cpp`
- `app/dialog/otioproperties/otiopropertiesdialog.h`, `otiopropertiesdialog.cpp`
- `app/common/otioutils.h`

#### Identified Bugs and Gaps in Current OTIO Implementation
1. **Critical Memory Leak & Data Loss Bug in `SaveOTIOTask::SerializeTrack` (`saveotio.cpp:172-181`)**:
   ```cpp
   } else if (dynamic_cast<TransitionBlock*>(block)) {
     auto otio_transition = new OTIO::Transition(block->GetLabel().toStdString());
     TransitionBlock* our_transition = static_cast<TransitionBlock*>(block);
     otio_transition->set_in_offset(our_transition->in_offset().toRationalTime());
     otio_transition->set_out_offset(our_transition->out_offset().toRationalTime());
     otio_block = new OTIO::Transition(); // BUG: Overwrites otio_transition with an empty transition!
   }
   ```
   `otio_block` is assigned a brand new empty `OTIO::Transition`, completely discarding `otio_transition` and leaking its memory while exporting an uninitialized transition!
2. **Markers Dropped**:
   - `SaveOTIOTask::SerializeTimeline` does not serialize `sequence->GetMarkers()`.
   - `LoadOTIOTask::Run` does not import `timeline->markers()` into `sequence->GetMarkers()`.
3. **Clip Speed/Reverse Ignored**:
   - `ClipBlock::speed()` and `ClipBlock::reverse()` are not serialized to `OTIO::LinearTimeWarp` effects, and imported time warps are ignored.
4. **Sequence Parameter Ambiguity**:
   - OTIO timeline specifications do not standardize sequence resolution/framerate. Olive relies on `OTIOPropertiesDialog` for user input instead of reading metadata dictionary values (`timeline->metadata()`).
5. **No Timeline-level Export Action**:
   - In `MainMenu` (`app/window/mainwindow/mainmenu.cpp:62-63`), `file_export_menu_` only contains `file_export_media_item_` (video render). It lacks `file_export_otio_item_` and `file_export_fcpxml_item_`.

#### Final Cut Pro 7 XML (xmeml) Status
- **Current State**: Zero implementation currently exists in Olive.
- **Architectural Advantage**: FCP7 XML is standard XML. Because Olive already heavily relies on Qt's `QXmlStreamReader` and `QXmlStreamWriter` for all its serialization, a native FCP7 XML parser/serializer can be built in pure C++17/Qt6 with **zero external dependencies**!
- It can be placed directly in `app/task/project/loadfcpxml/` and `app/task/project/savefcpxml/` (or a unified `app/task/project/fcpxml/` module), guaranteeing that FCP7 XML is always available even when the OTIO C++ library is not installed.

---

### 2.3 Timeline Elements & Data Structures in Olive

The following table details the mapping of timeline concepts to Olive's internal classes:

| Timeline Element | Olive In-Memory Data Structure | Key Properties & Methods |
|---|---|---|
| **Sequence (Timeline)** | `Sequence` (`app/node/project/sequence/sequence.h`) inherits `ViewerOutput` -> `Node` | `GetVideoParams()`, `GetAudioParams()`, `GetMarkers()`, `GetTracks()`, `track_list(Track::Type)` |
| **Video Track** | `Track` (`app/node/output/track/track.h`) with `Track::kVideo` | `type()`, `Index()`, `Blocks()`, `is_enabled()`, `is_locked()`, `track_length()` |
| **Audio Track** | `Track` (`app/node/output/track/track.h`) with `Track::kAudio` | `type()`, `Index()`, `Blocks()`, `is_enabled()`, `is_locked()`, `track_length()` |
| **Clip Item** | `ClipBlock` (`app/node/block/clip/clip.h`) inherits `Block` -> `Node` | `in()`, `out()`, `length()`, `media_in()`, `speed()`, `reverse()`, `block_links()` |
| **Gap Item** | `GapBlock` (`app/node/block/gap/gap.h`) inherits `Block` -> `Node` | `in()`, `out()`, `length()` |
| **Transition** | `TransitionBlock` (`app/node/block/transition/transition.h`) / `CrossDissolveTransition` | `in_offset()`, `out_offset()`, `length()`, connected `kInBlockInput` & `kOutBlockInput` |
| **Media Reference** | `Footage` (`app/node/project/footage/footage.h`) inherits `ViewerOutput` -> `Node` | `filename()`, `GetVideoParams()`, `GetAudioParams()`, `ProbeMedia()` |
| **Markers** | `TimelineMarker` & `TimelineMarkerList` (`app/timeline/timelinemarker.h`) | `time()` (`TimeRange`), `name()` (`QString`), `color()` (`int`) |
| **Timebase & Rate** | `rational` (`ext/core/include/olive/core/util/rational.h`), `Timecode` | `AVRational`, `time_to_timestamp()`, `timestamp_to_time()`, `timebase_is_drop_frame()` |
| **Connected Nodes** | DAG Nodes (`TransformDistortNode`, `VolumeNode`) | Positioned in context via `SetNodePositionInContext()` |

---

### 2.4 High-Fidelity Interchange Requirements: Olive <-> Kdenlive <-> Premiere Pro

Achieving high-fidelity roundtrip between Olive, Kdenlive, and Adobe Premiere Pro requires strict adherence to the Apple Final Cut Pro 7 XML (xmeml version 4/5) specification and OTIO 0.14+ schema.

#### 1. Frame Rate & Timebase Representation
In FCP7 XML, rate is expressed as an integer `<timebase>` and a boolean `<ntsc>` flag:

| Target Frame Rate | Exact Rational in Olive | FCP7 XML `<timebase>` | FCP7 XML `<ntsc>` | OTIO RationalTime rate |
|---|---|---|---|---|
| **23.976 fps** | 24000 / 1001 | 24 | TRUE | 24000 / 1001 (or 23.976) |
| **24.0 fps** | 24 / 1 | 24 | FALSE | 24.0 |
| **25.0 fps (PAL)** | 25 / 1 | 25 | FALSE | 25.0 |
| **29.97 fps (NTSC)**| 30000 / 1001 | 30 | TRUE | 30000 / 1001 (or 29.97) |
| **30.0 fps** | 30 / 1 | 30 | FALSE | 30.0 |
| **50.0 fps** | 50 / 1 | 50 | FALSE | 50.0 |
| **59.94 fps** | 60000 / 1001 | 60 | TRUE | 60000 / 1001 (or 59.94) |
| **60.0 fps** | 60 / 1 | 60 | FALSE | 60.0 |

- Conversion to frames: `int64_t frame = Timecode::time_to_timestamp(time, rational(1) / fps, Timecode::kRound);`
- Conversion from frames: `rational time = Timecode::timestamp_to_time(frame, rational(1) / fps);`

#### 2. Sequence XML Structure
```xml
<!DOCTYPE xmeml>
<xmeml version="5">
  <sequence id="sequence-1">
    <name>Sequence 1</name>
    <duration>720</duration>
    <rate>
      <timebase>24</timebase>
      <ntsc>FALSE</ntsc>
    </rate>
    <timecode>
      <rate><timebase>24</timebase><ntsc>FALSE</ntsc></rate>
      <string>00:00:00:00</string>
      <frame>0</frame>
      <displayformat>NDF</displayformat>
    </timecode>
    <media>
      <video>
        <format>
          <samplecharacteristics>
            <width>1920</width>
            <height>1080</height>
            <pixelaspectratio>square</pixelaspectratio>
            <rate><timebase>24</timebase><ntsc>FALSE</ntsc></rate>
          </samplecharacteristics>
        </format>
        <track> <!-- Video Track V1 -->
          ...
        </track>
      </video>
      <audio>
        <numOutputChannels>2</numOutputChannels>
        <format>
          <samplecharacteristics>
            <samplerate>48000</samplerate>
            <depth>16</depth>
          </samplecharacteristics>
        </format>
        <track> <!-- Audio Track A1 -->
          ...
        </track>
      </audio>
    </media>
    <marker>
      <name>Marker 1</name>
      <comment>Note</comment>
      <in>120</in>
      <out>-1</out>
    </marker>
  </sequence>
</xmeml>
```

#### 3. Clip Placement & Timing Semantics
- In FCP7 XML, clips on a track have:
  - `<start>`: Frame on timeline where clip begins (`ClipBlock::in()`).
  - `<end>`: Frame on timeline where clip ends (`ClipBlock::out()`).
  - `<in>`: Source frame inside the media file where playback starts (`ClipBlock::media_in()`).
  - `<out>`: Source frame inside media file where playback ends (`media_in() + length()`).
  - `<duration>`: Total length of source media in frames.
- **Handling Gaps**: In FCP7 XML, gaps are implicit between `<clipitem>` entries (e.g., Clip A ends at 100, Clip B starts at 200 -> implicit 100-frame gap). In OTIO, gaps must be explicitly created as `OTIO::Gap`.
- **Linked Audio/Video Clips (`<link>`)**:
  To ensure Premiere and Kdenlive move paired audio and video clips together, Olive must serialize `<link>` tags under each `<clipitem>`:
  ```xml
  <link>
    <linkclipref>clipitem-video-1</linkclipref>
    <mediatype>video</mediatype>
    <trackindex>1</trackindex>
    <clipindex>1</clipindex>
  </link>
  <link>
    <linkclipref>clipitem-audio-1</linkclipref>
    <mediatype>audio</mediatype>
    <trackindex>1</trackindex>
    <clipindex>1</clipindex>
    <groupindex>1</groupindex>
  </link>
  ```
  On import, clips with matching `<link>` references must be connected via `ClipBlock::block_links()`.

#### 4. Media Path Resolution (`<file>`)
- FCP7 XML specifies `<pathurl>file:///path/to/media.mp4</pathurl>`.
- Path resolution rules:
  - Must percent-encode URLs properly (e.g. spaces as `%20`).
  - Must support relative paths when migrating between machines/platforms.
  - Importer must handle missing files gracefully with relink prompts or placeholder clips.

#### 5. Transitions
- FCP7 XML represents transitions using `<transitionitem>` positioned with `<start>`, `<end>`, and `<alignment>`:
  - `center`: Centered on edit point.
  - `start-on-edit`: Starts at cut.
  - `end-on-edit`: Ends at cut.
- Supported transitions across NLEs: `<name>Cross Dissolve</name>`, `<effectid>Cross Dissolve</effectid>`, `<effecttype>transition</effecttype>`.

---

## 3. Requirement R4: Linux Packaging Automation (AppImage & Flatpak)

### 3.1 Survey of Existing Packaging Assets

The repository contains:
1. `app/packaging/linux/AppRun`:
   - A shell script designed to launch `olive-editor` and wait for `olive-crashhandler`.
   - **Critical defect**: It does **not** configure `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, or `XDG_DATA_DIRS`. If packaged as-is, it relies on host system libraries and crashes on clean installations.
2. `app/packaging/linux/CMakeLists.txt`:
   - Configures and installs `org.olivevideoeditor.Olive.desktop`, `org.olivevideoeditor.Olive.appdata.xml`, icons (16x16 to 512x512), and MIME types.
3. `app/packaging/linux/org.olivevideoeditor.Olive.desktop`:
   - Desktop entry specifying `Exec=olive-editor %f`, `Icon=org.olivevideoeditor.Olive`, `MimeType=application/vnd.olive-project;`.
4. `app/packaging/linux/org.olivevideoeditor.Olive.appdata.xml.in`:
   - Valid AppStream metadata with multilingual descriptions and OARS content ratings.
5. `docker/scripts/build_olive.sh`:
   - Legacy script invoking `linuxdeployqt-x86_64.AppImage` targeting **Qt5**.
   - Not compatible with Qt6 builds without substantial breakage.

---

### 3.2 Build and Runtime Dependency Tree

For a pure **C++17 / Qt6** Linux build, the dependencies are:

| Component | Minimum Version | Libraries / Headers | Package (Ubuntu 24.04 / Debian) |
|---|---|---|---|
| **Build Tools** | CMake 3.21+, Ninja 1.10+, GCC 11+ / Clang 14+ | `cmake`, `ninja`, `pkg-config`, `git` | `build-essential cmake ninja-build pkg-config` |
| **Qt6 Core & GUI** | Qt 6.4+ | `Qt6::Core`, `Qt6::Gui`, `Qt6::Widgets`, `Qt6::Svg` | `qt6-base-dev qt6-tools-dev qt6-tools-dev-tools` |
| **Qt6 OpenGL** | Qt 6.4+ | `Qt6::OpenGL`, `Qt6::OpenGLWidgets`, `libGL` | `libqt6opengl6-dev libgl1-mesa-dev` |
| **FFmpeg** | 5.0+ (5.1 / 6.1 / 7.0) | `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`, `libavfilter` | `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libavfilter-dev` |
| **OpenColorIO** | 2.1+ | `OpenColorIO::OpenColorIO` | `libopencolorio-dev` |
| **OpenImageIO** | 2.3+ | `OpenImageIO::OpenImageIO`, `OpenImageIO::OpenImageIO_Util` | `libopenimageio-dev openimageio-tools` |
| **OpenEXR / Imath** | 3.1+ | `OpenEXR::OpenEXR`, `Imath::Imath` | `libopenexr-dev libimath-dev` |
| **Audio I/O** | PortAudio v19 | `portaudio-2.0` | `portaudio19-dev` |
| **X11 / Wayland** | System | `libxkbcommon`, `libxkbcommon-x11`, `libxcb-cursor` | `libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0` |

---

### 3.3 Reproducible AppImage Generation Architecture

#### Pitfalls of `linuxdeployqt` with Qt6
`linuxdeployqt` was designed for Qt4/Qt5. On Qt6, it routinely refuses to run or copies wrong plugin paths unless modified.
The industry-standard solution for Qt6 AppImages is either:
1. **`linuxdeploy` + `linuxdeploy-plugin-qt` + `linuxdeploy-plugin-appimage`**
2. Or a deterministic, script-driven **AppDir bundler** using standard toolchain binaries (`patchelf`, `appimagetool`, and Qt6 deployment commands).

#### Complete Recipe: `scripts/build-appimage.sh`
Below is the clean, self-contained architecture for generating an AppImage:

```bash
#!/usr/bin/env bash
set -euo pipefail

# 1. Setup paths
BUILD_DIR="${1:-build-linux-release}"
APPDIR="$(pwd)/AppDir"
OUTPUT_DIR="$(pwd)/dist"
mkdir -p "${APPDIR}" "${OUTPUT_DIR}"

# 2. Stage installation into AppDir
cmake --install "${BUILD_DIR}/app" --prefix "${APPDIR}/usr"

# 3. Create root links required by AppImage spec
ln -sf usr/share/applications/org.olivevideoeditor.Olive.desktop "${APPDIR}/"
ln -sf usr/share/icons/hicolor/256x256/apps/org.olivevideoeditor.Olive.png "${APPDIR}/"
ln -sf org.olivevideoeditor.Olive.png "${APPDIR}/.DirIcon"

# 4. Copy Qt6 platform and imageformat plugins
QT_PLUGIN_DIR="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)"
mkdir -p "${APPDIR}/usr/plugins"
cp -r "${QT_PLUGIN_DIR}/platforms" "${APPDIR}/usr/plugins/"
cp -r "${QT_PLUGIN_DIR}/imageformats" "${APPDIR}/usr/plugins/"
cp -r "${QT_PLUGIN_DIR}/iconengines" "${APPDIR}/usr/plugins/" 2>/dev/null || true
cp -r "${QT_PLUGIN_DIR}/platformthemes" "${APPDIR}/usr/plugins/" 2>/dev/null || true

# 5. Bundle shared libraries (Qt6, FFmpeg, OCIO, OIIO, PortAudio)
mkdir -p "${APPDIR}/usr/lib"
python3 - << 'PYEOF'
import subprocess, shutil, os

appdir_lib = "AppDir/usr/lib"
binaries = ["AppDir/usr/bin/olive-editor"]
for root, _, files in os.walk("AppDir/usr/plugins"):
    for f in files:
        if f.endswith(".so"):
            binaries.append(os.path.join(root, f))

copied = set()
for binary in binaries:
    output = subprocess.check_output(["ldd", binary], text=True)
    for line in output.splitlines():
        parts = line.strip().split(" => ")
        if len(parts) == 2:
            src = parts[1].split(" ")[0]
            if src.startswith(("/usr/lib", "/usr/local/lib")) and not any(k in src for k in ["libc.so", "libm.so", "libpthread.so", "libdl.so", "libGL.so", "libdrm.so", "libX11.so"]):
                libname = os.path.basename(src)
                if libname not in copied:
                    shutil.copy2(src, os.path.join(appdir_lib, libname))
                    copied.add(libname)
PYEOF

# 6. Install portable AppRun script
cat << 'RUNEOF' > "${APPDIR}/AppRun"
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
export QML2_IMPORT_PATH="${HERE}/usr/qml"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "${HERE}/usr/bin/olive-editor" "$@"
RUNEOF
chmod +x "${APPDIR}/AppRun"

# 7. Package with appimagetool
export ARCH=x86_64
appimagetool "${APPDIR}" "${OUTPUT_DIR}/Olive-x86_64.AppImage"
```

---

### 3.4 Flatpak Packaging Architecture

Flatpak provides an isolated, sandboxed runtime environment for Linux desktops. Using the **KDE Application Runtime (`org.kde.Platform` 6.x)** provides a native Qt6 environment with complete Wayland and X11 support.

#### Flatpak Manifest: `packaging/flatpak/org.olivevideoeditor.Olive.json`
```json
{
  "app-id": "org.olivevideoeditor.Olive",
  "runtime": "org.kde.Platform",
  "runtime-version": "6.8",
  "sdk": "org.kde.Sdk",
  "command": "olive-editor",
  "finish-args": [
    "--share=ipc",
    "--socket=x11",
    "--socket=wayland",
    "--socket=pulseaudio",
    "--device=dri",
    "--filesystem=host",
    "--talk-name=org.freedesktop.Notifications"
  ],
  "cleanup": [
    "/include",
    "/lib/pkgconfig",
    "/share/man"
  ],
  "modules": [
    {
      "name": "portaudio",
      "buildsystem": "autotools",
      "sources": [
        {
          "type": "archive",
          "url": "http://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz",
          "sha256": "47ef442e38cde71054cb500331a476c84720335d90ed8d3d88d29a6d20195b03"
        }
      ]
    },
    {
      "name": "imath",
      "buildsystem": "cmake-ninja",
      "sources": [
        {
          "type": "archive",
          "url": "https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.1.9.tar.gz",
          "sha256": "f1d8aacd4610b55769f7470f5be97657ba4e5fa5064b237f94d36f86dbce269b"
        }
      ]
    },
    {
      "name": "openexr",
      "buildsystem": "cmake-ninja",
      "sources": [
        {
          "type": "archive",
          "url": "https://github.com/AcademySoftwareFoundation/openexr/archive/refs/tags/v3.2.1.tar.gz",
          "sha256": "61e520b7ab3ba9254512270913f99e46a78888e7b3992b19280d0d86927d3122"
        }
      ]
    },
    {
      "name": "opencolorio",
      "buildsystem": "cmake-ninja",
      "config-opts": [
        "-DOCIO_BUILD_APPS=OFF",
        "-DOCIO_BUILD_PYTHON=OFF",
        "-DOCIO_BUILD_TESTS=OFF",
        "-DOCIO_BUILD_GPU_TESTS=OFF"
      ],
      "sources": [
        {
          "type": "archive",
          "url": "https://github.com/AcademySoftwareFoundation/OpenColorIO/archive/refs/tags/v2.3.0.tar.gz",
          "sha256": "55c4149cd2bb6d45672a08c0ef0beae9f56e54ee0d8ff3d100067ff5ea999908"
        }
      ]
    },
    {
      "name": "openimageio",
      "buildsystem": "cmake-ninja",
      "config-opts": [
        "-DOIIO_BUILD_TESTS=OFF",
        "-DOIIO_BUILD_TOOLS=OFF",
        "-DUSE_PYTHON=OFF"
      ],
      "sources": [
        {
          "type": "archive",
          "url": "https://github.com/AcademySoftwareFoundation/OpenImageIO/archive/refs/tags/v2.5.4.0.tar.gz",
          "sha256": "01fb61680d22ebbfd5cf59b13904f44fa121ea24bf782c3c6f6634c01f687449"
        }
      ]
    },
    {
      "name": "olive",
      "buildsystem": "cmake-ninja",
      "config-opts": [
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        "-DBUILD_QT6=ON",
        "-DBUILD_TESTS=OFF",
        "-DUSE_WERROR=OFF"
      ],
      "sources": [
        {
          "type": "dir",
          "path": "../.."
        }
      ]
    }
  ]
}
```

---

## 4. Gauntlet & Quality Gate Infrastructure

### 4.1 Mechanics of `scripts/gauntlet.py`

`scripts/gauntlet.py` is the gating harness for Olive. It enforces the rule that no pull request or build can be marked passed unless all stages execute synchronously and succeed.

#### Detailed Execution Trace (`--preset linux-asan --jobs 4`)
1. **CLI Argument Resolution**:
   - `preset = "linux-asan"`
   - `jobs = 4`
   - `gpu = False` (sets `-DBUILD_GPU_TESTS=OFF`)
   - `fresh = False` (reuses `build-linux-asan/` directory)
   - `repeat = 1`, `soak_seconds = 0`
2. **Deterministic Isolation**:
   - Creates a unique UTC timestamp subdirectory under `ROOT/qa-results/<stamp>/`.
   - Replaces `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, and `XDG_DATA_HOME` with isolated subfolders inside `qa-results/<stamp>/`.
   - Guarantees user configuration files (`~/.config/olivevideoeditor.org/`) cannot alter test behavior or leak test states.
3. **Execution Steps**:
   - `git-head`: Validates commit hash (`git rev-parse HEAD`).
   - `git-status`: Inspects uncommitted changes (`git status --short`).
   - `submodules`: Asserts core and KDDockWidgets submodule status (`git submodule status`).
   - `diff`: Records patch contents (`git diff --binary`).
   - `cmake-version`: Validates toolchain version (`cmake --version`).
   - `configure`: Runs `cmake --preset linux-asan -B build-linux-asan -DBUILD_GPU_TESTS=OFF`.
   - `build`: Executes `cmake --build build-linux-asan -j 4`.
   - `test-inventory`: Queries registered tests via `ctest --test-dir build-linux-asan --show-only=json-v1`. Enforces that test count is greater than zero.
   - `test-0001`: Executes `ctest --test-dir build-linux-asan --output-on-failure --no-tests=error --timeout 120 --output-junit junit-0001.xml`.
   - `save()`: Writes `qa-results/<stamp>/report.json` with step durations, command lines, and status.

#### Empirical Verification Results
During this survey, `python3 scripts/gauntlet.py --preset linux-asan --jobs 4` was executed on the working tree:
- Status: **`passed`**
- Registered test executables: **7**
- Test execution time: **6.329s**
- Report location: `qa-results/20260920T141009.116996Z/report.json`
- Zero memory leaks and zero sanitizer failures detected across all suites.

---

### 4.2 CMake Presets, ASan Flags, and CTest Architecture

#### CMake Presets Configuration (`CMakePresets.json`)
The `linux-asan` preset defines:
```json
{
  "name": "linux-asan",
  "generator": "Ninja",
  "binaryDir": "${sourceDir}/build-linux-asan",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Debug",
    "BUILD_QT6": "ON",
    "BUILD_TESTS": "ON",
    "USE_WERROR": "OFF",
    "OLIVECORE_BUILD_TESTS": "OFF",
    "ENABLE_SANITIZER_ADDRESS": "ON",
    "ENABLE_SANITIZER_UNDEFINED_BEHAVIOR": "ON"
  }
}
```

#### Sanitizer Implementation (`cmake/Sanitizers.cmake`)
When `ENABLE_SANITIZER_ADDRESS` and `ENABLE_SANITIZER_UNDEFINED_BEHAVIOR` are enabled:
```cmake
target_compile_options(${target} INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(${target} INTERFACE -fsanitize=address,undefined)
```
- `-fsanitize=address`: Injects AddressSanitizer runtime to catch out-of-bounds accesses, heap-use-after-free, stack-use-after-return, and memory leaks.
- `-fsanitize=undefined`: Injects UndefinedBehaviorSanitizer (UBSan) to trap null pointer dereferences, integer overflows, alignment issues, and illegal casts.
- `-fno-omit-frame-pointer`: Guarantees complete, clean backtraces in crash logs.

#### CTest Test Harness Generation (`tests/CMakeLists.txt`)
Olive uses a custom CMake macro `olive_add_test(GROUP NAME SOURCE [GPU])`:
1. Scans the C++ source file for `OLIVE_ADD_TEST(FunctionName)`.
2. Dynamically generates a `main(int argc, char** argv)` function in `${CMAKE_CURRENT_BINARY_DIR}`:
   - Sets `QCoreApplication app(argc, argv);` and `QStandardPaths::setTestModeEnabled(true);`.
   - Iterates through all registered test functions, invoking `olive::TestFunctionName()`.
   - Checks return code: `OLIVE_TEST_SUCCESS` (-1) indicates success; any positive integer indicates the line number of an assertion failure (`OLIVE_ASSERT` / `OLIVE_ASSERT_EQUAL`).
3. Links the resulting test binary against `libolive-editor` object files and project libraries.
4. Registers the test with CTest (`add_test`) with a 120-second timeout.

---

### 4.3 Acceptance Criteria & Zero Leak / Zero Assertion Assurance

To satisfy the acceptance criteria of **0 memory leaks and 0 assertion failures**:

1. **RAII Memory Ownership**:
   - Raw pointers (`new`/`delete`) must never be used for lifecycle management.
   - Qt `QObject` parent-child hierarchies (`setParent(parent)`) automatically delete child objects when the parent is deleted.
   - When transferring objects out of a task or temporary scope, use `std::unique_ptr` with explicit releases or `QScopedPointer`.
2. **Fixing the Transition Memory Leak**:
   - In `SaveOTIOTask::SerializeTrack`, replace the faulty line 180 (`otio_block = new OTIO::Transition();`) with `otio_block = otio_transition;`.
3. **Thread Safety & QObject Thread Affinity**:
   - Tasks like `LoadOTIOTask` and `LoadFCPXMLTask` execute in a worker thread.
   - All newly allocated `QObject` nodes (`Project`, `Sequence`, `Track`, `ClipBlock`) must be moved to the main thread before the task terminates:
     `project_->moveToThread(qApp->thread());`
   - Failure to do so causes Qt assertion failures when events or signals are dispatched on the main UI thread.
4. **Pure Qt6 String & XML Practices**:
   - Avoid legacy Qt5 methods (`QString::null`, `QXmlStreamReader::readElementText(QXmlStreamReader::IncludeChildElements)` issues).
   - Use `QStringLiteral` and `QLatin1String` for tag names to minimize heap allocations during XML parsing.
5. **Regression Test Suite Integration**:
   - For R3, a new test suite `tests/project/fcpxml-tests.cpp` and `tests/project/otio-tests.cpp` must be added to `tests/project/CMakeLists.txt`.
   - Tests must verify:
     - Full roundtrip: create `Sequence` -> serialize to FCPXML/OTIO -> deserialize back -> assert exact equality of in/out frames, track counts, and markers.
     - Malformed XML handling: assert `kXmlError` is returned safely without crashing.
     - Out-of-order clips, empty tracks, and zero-duration gaps.

---

## 5. Actionable Roadmap & Recommendations

### Phase 1: Fix OTIO Serialization & Add XML Interop Engine
1. **Fix OTIO Bugs**: Correct `SaveOTIOTask::SerializeTrack` line 180 transition allocation, add marker serialization/deserialization, add clip speed/reverse serialization.
2. **Implement FCP7 XML Parser & Serializer**:
   - Create `app/task/project/fcpxml/fcpxmlreader.h` and `.cpp` using `QXmlStreamReader`.
   - Create `app/task/project/fcpxml/fcpxmlwriter.h` and `.cpp` using `QXmlStreamWriter`.
   - Create `LoadFCPXMLTask` and `SaveFCPXMLTask` under `app/task/project/loadfcpxml/` and `savefcpxml/`.
3. **Update Main Menu**:
   - In `app/window/mainwindow/mainmenu.cpp`: add `file_export_fcpxml_item_` and `file_export_otio_item_` under File -> Export.
   - In `app/core.cpp`: add `DialogExportFCPXMLShow()` and `DialogExportOTIOShow()`. Add `.xml` filter to `GetProjectFilter()`.

### Phase 2: Linux Packaging Automation
1. **AppImage**:
   - Add `scripts/build-appimage.sh` with the complete Qt6 deployment recipe.
   - Update `app/packaging/linux/AppRun` to export `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, and `XDG_DATA_DIRS`.
2. **Flatpak**:
   - Add `packaging/flatpak/org.olivevideoeditor.Olive.json` targeting `org.kde.Platform` 6.8 / 6.10.
   - Add automated Flatpak lint check script.

### Phase 3: Gauntlet & Regression Verification
1. Add `tests/project/interchange-tests.cpp` covering FCPXML and OTIO roundtrips.
2. Execute `python3 scripts/gauntlet.py --preset linux-asan --jobs 4` to guarantee 0 leaks, 0 assertion failures, and 100% test passage.
