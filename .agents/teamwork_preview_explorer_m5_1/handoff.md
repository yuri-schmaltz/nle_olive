# Comprehensive Survey & Analysis Report: Linux Packaging, Build Systems, and Qt6 AppImage Integration

**Agent**: `teamwork_preview_explorer_m5_1`  
**Role**: Read-only Explorer / Investigator / Synthesizer  
**Milestone**: M5 (Linux Packaging Automation)  
**Parent Orchestrator**: `teamwork_preview_suborch_m5` (`d8291db2-3b3d-41ad-a12d-27615886bd25`)  
**Date**: 2026-09-20  

---

## 1. Observation

### 1.1 Existing Packaging Files & Scripts

#### A. Existing `app/packaging/linux/AppRun`
Inspection of `/home/yuri/Documentos/olive/app/packaging/linux/AppRun` (18 lines):
```bash
1: #!/usr/bin/env bash
2: 
3: APPDIR=$(readlink -f $(dirname "$0"))
4: 
5: # Custom AppRun that ensures the AppImage doesn't dismount before olive-crashhandler exits
6: 
7: # Run main program
8: "$APPDIR/usr/bin/olive-editor" "$@"
9: 
10: # Wait arbitrary amount of time
11: sleep 5
12: 
13: # While olive-crashhandler exists, keep sleeping
14: while [[ $(ps -aux | grep olive-crashhandler | grep -v grep) ]]
15: do
16: 	sleep 5
17: done
```

Direct observations & defects:
- **No environment variable exports**: Neither `LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, nor `XDG_DATA_DIRS` is set.
- **Unconditional 5-second sleep (`sleep 5` at line 11)**: Every execution—including `olive-editor --version`, `--help`, or normal shutdown—unconditionally sleeps 5 seconds before returning.
- **Exit code swallowed**: The exit status (`$?`) of `"$APPDIR/usr/bin/olive-editor" "$@"` is completely discarded because subsequent lines execute `sleep 5` and a `while` loop. CLI calls (such as `--export` or `--version`) will never propagate exit codes.
- **Inefficient and fragile process polling (lines 14-17)**: `ps -aux | grep olive-crashhandler | grep -v grep` is executed in a polling loop. `olive-crashhandler` is only built when `USE_CRASHPAD` is enabled (`app/CMakeLists.txt:146-148`), which is disabled in standard builds.

#### B. Legacy Packaging Script `docker/scripts/build_olive.sh`
Inspection of `/home/yuri/Documentos/olive/docker/scripts/build_olive.sh`:
```bash
10: cmake --install app --prefix appdir/usr
...
17: /usr/local/linuxdeployqt-x86_64.AppImage \
18:   appdir/usr/share/applications/org.olivevideoeditor.Olive.desktop \
19:   -appimage \
20:   -exclude-libs=\
21: libQt5Pdf.so,\
22: libQt5Qml.so,\
23: libQt5QmlModels.so,\
24: libQt5Quick.so,\
25: libQt5VirtualKeyboard.so \
26:   -bundle-non-qt-libs \
27:   -executable=appdir/usr/bin/crashpad_handler \
28:   -executable=appdir/usr/bin/minidump_stackwalk \
29:   -executable=appdir/usr/bin/olive-crashhandler \
30:   --appimage-extract-and-run
```
Direct observations:
- Targets **Qt5** exclusively (`libQt5*`).
- Invokes legacy `linuxdeployqt`, which fails on Qt6 due to changed plugin architecture and directory layout.
- Assumes crashpad executables exist (`crashpad_handler`, `minidump_stackwalk`, `olive-crashhandler`).

#### C. Root `packaging/` Directory
- A root-level `packaging/` directory does not currently exist in the repository.
- Neither `packaging/linux/build_appimage.sh` nor `packaging/flatpak/org.olivevideoeditor.Olive.json` has been created yet.

---

### 1.2 Desktop Integration, Icons, MIME Types & AppStream Metadata

#### A. Desktop File (`app/packaging/linux/org.olivevideoeditor.Olive.desktop`)
Content:
```ini
1: [Desktop Entry]
2: Name=Olive
3: Comment=Professional open-source non-linear video editor
4: Comment[fr]=Éditeur vidéo non-linéaire open-source professionnel
5: Comment[it]=Programma di montaggio video professionale open-source
6: Comment[id]=Aplikasi edit video yang non-linier, profesional serta sumbernya terbuka.
7: Exec=olive-editor %f
8: Icon=org.olivevideoeditor.Olive
9: Terminal=false
10: Type=Application
11: Categories=AudioVideo;Recorder;
12: MimeType=application/vnd.olive-project;
13: StartupNotify=true
```
- Empirical validation command: `desktop-file-validate app/packaging/linux/org.olivevideoeditor.Olive.desktop`
- Result: **Passed with 0 errors**.
- Identified omissions:
  - Missing standard NLE categories: should include `AudioVideoEditing;` and `Video;` (`Categories=AudioVideo;Video;AudioVideoEditing;Recorder;`).
  - Missing desktop search keywords: `Keywords=video;editor;audio;non-linear;nle;cutting;timeline;`
  - Missing WM class association: `StartupWMClass=olive-editor` (in `app/main.cpp:123`, `QCoreApplication::setApplicationName("Olive")` and line 124 `QGuiApplication::setDesktopFileName("org.olivevideoeditor.Olive")`).

#### B. Icons (`app/packaging/linux/icons/`)
- Directory structure:
  - `16x16/`, `32x32/`, `48x48/`, `64x64/`, `128x128/`, `256x256/`, `512x512/`
  - In each size:
    - `org.olivevideoeditor.Olive.png` (Application icon)
    - `application-vnd.olive-project.png` (MIME type icon)
- No scalable vector icon (`.svg`) exists for the application icon.
- `CMakeLists.txt` (`app/packaging/linux/CMakeLists.txt:38-47`) installs each resolution into:
  - `share/icons/hicolor/${size}x${size}/apps/org.olivevideoeditor.Olive.png`
  - `share/icons/hicolor/${size}x${size}/mimetypes/application-vnd.olive-project.png`
- AppImage specification requirement:
  - The root of `AppDir` must contain:
    - `AppDir/org.olivevideoeditor.Olive.png`
    - `AppDir/.DirIcon`
    - `AppDir/org.olivevideoeditor.Olive.desktop`

#### C. MIME Types (`app/packaging/linux/org.olivevideoeditor.Olive.xml`)
Content:
```xml
1: <?xml version="1.0"?>
2: <mime-info xmlns='http://www.freedesktop.org/standards/shared-mime-info'>
3:   <mime-type type="application/vnd.olive-project">
4:     <comment>Olive project</comment>
5:     <glob pattern="*.ove"/>
6:   </mime-type>
7: </mime-info>
```
- Empirical validation: `xmllint --noout app/packaging/linux/org.olivevideoeditor.Olive.xml` passed.
- Omission: Only matches `*.ove`. Olive also supports uncompressed project files (`*.ovexml`, handled in `serializer.cpp:822` and `main.cpp:95`). `*.ovexml` should be added.

#### D. AppStream Metadata (`app/packaging/linux/org.olivevideoeditor.Olive.appdata.xml.in`)
- Configured by CMake to `share/metainfo/org.olivevideoeditor.Olive.appdata.xml`.
- Empirical validation command:
  `appstreamcli validate build-linux-release/app/packaging/linux/org.olivevideoeditor.Olive.appdata.xml`
- Verbatim tool output:
  ```
  I: org.olivevideoeditor.Olive:7: developer-name-tag-deprecated
  E: org.olivevideoeditor.Olive:19: metainfo-localized-description-tag description
  E: org.olivevideoeditor.Olive:20: metainfo-localized-description-tag description
  E: org.olivevideoeditor.Olive:21: metainfo-localized-description-tag description
  E: org.olivevideoeditor.Olive:22: metainfo-localized-description-tag description
  E: org.olivevideoeditor.Olive:23: metainfo-localized-description-tag description
  E: org.olivevideoeditor.Olive:24: metainfo-localized-description-tag description
  E: org.olivevideoeditor.Olive:25: metainfo-localized-description-tag description
  E: org.olivevideoeditor.Olive:26: metainfo-localized-description-tag description
  E: org.olivevideoeditor.Olive:27: metainfo-localized-description-tag description
  E: org.olivevideoeditor.Olive:~: desktop-app-launchable-missing
  I: org.olivevideoeditor.Olive:~: developer-info-missing
  ✘ Validação falhou: erros: 10, infos: 2, pedante: 2
  ```
- Fatal defects identified:
  1. Lines 19-27 have `<description xml:lang="...">` without paragraph tags `<p>` (e.g. line 19: `<description xml:lang="de_DE">Olive ist ...</description>`). AppStream specification requires paragraph child tags `<p>` inside `<description>`.
  2. Missing `<launchable type="desktop-id">org.olivevideoeditor.Olive.desktop</launchable>`.
  3. Deprecated `<developer_name>` tag (modern format: `<developer id="org.olivevideoeditor"><name>Olive Team</name></developer>`).

---

### 1.3 CMake Targets, Install Layout, Binary Inspection & RPATH

#### A. Target Definition & Install Rules
- Target name: `olive-editor` defined in `app/CMakeLists.txt:82-86`:
  ```cmake
  add_executable(olive-editor
    main.cpp
    $<TARGET_OBJECTS:libolive-editor>
    $<TARGET_OBJECTS:olive-version-obj>
  )
  ```
- Install rule: `app/CMakeLists.txt:126`:
  ```cmake
  elseif(UNIX)
    install(TARGETS olive-editor RUNTIME DESTINATION bin)
  endif()
  ```
- Submodule install rules:
  - `ext/CMakeLists.txt`:
    ```cmake
    add_subdirectory(core EXCLUDE_FROM_ALL)
    add_subdirectory(KDDockWidgets EXCLUDE_FROM_ALL)
    ```
    `EXCLUDE_FROM_ALL` prevents `olivecore` or `kddockwidgets` from installing standalone files during root install. Both compile to static libraries (`libolivecore.a` and `libkddockwidgets-qt6.a`) and are statically linked into `olive-editor`.
  - `app/packaging/CMakeLists.txt`:
    ```cmake
    if(UNIX AND NOT APPLE)
      add_subdirectory(linux)
    endif()
    ```

#### B. Destination Hierarchy from `cmake --install`
Empirical installation command executed to staging directory:
`cmake --install build-linux-release --prefix /tmp/olive-test-install`
Resulting file manifest:
```
/tmp/olive-test-install/
├── bin/
│   └── olive-editor
└── share/
    ├── applications/
    │   └── org.olivevideoeditor.Olive.desktop
    ├── icons/
    │   └── hicolor/
    │       ├── 16x16/{apps,mimetypes}/*.png
    │       ├── 32x32/{apps,mimetypes}/*.png
    │       ├── 48x48/{apps,mimetypes}/*.png
    │       ├── 64x64/{apps,mimetypes}/*.png
    │       ├── 128x128/{apps,mimetypes}/*.png
    │       ├── 256x256/{apps,mimetypes}/*.png
    │       └── 512x512/{apps,mimetypes}/*.png
    ├── metainfo/
    │   └── org.olivevideoeditor.Olive.appdata.xml
    └── mime/
        └── packages/
            └── org.olivevideoeditor.Olive.xml
```

#### C. Binary Inspection, NEEDED Libraries & RPATH
- Binary file: `build-linux-release/app/olive-editor` (15,019,720 bytes).
- Dynamic section inspection:
  `readelf -d build-linux-release/app/olive-editor | grep -E 'RPATH|RUNPATH|NEEDED'`
  ```
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libOpenColorIO.so.2.1]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libOpenImageIO.so.2.4]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libOpenImageIO_Util.so.2.4]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libOpenEXR-3_1.so.30]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libQt6OpenGLWidgets.so.6]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libavutil.so.58]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libavcodec.so.60]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libavformat.so.60]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libavfilter.so.9]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libswscale.so.7]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libswresample.so.4]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libportaudio.so.2]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libQt6DBus.so.6]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libImath-3_1.so.29]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libm.so.6]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libQt6OpenGL.so.6]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libQt6Widgets.so.6]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libQt6Gui.so.6]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libGL.so.1]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libQt6Core.so.6]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libstdc++.so.6]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libgcc_s.so.1]
  0x0000000000000001 (NEEDED)             Biblioteca Compartilhada [libc.so.6]
  ```
- RPATH verification:
  `readelf -d build-linux-release/app/olive-editor | grep -i path` returned exit code 1 (no entries).
- **Critical finding**: `olive-editor` has **no embedded RPATH or RUNPATH**. It relies entirely on host library search paths (`/etc/ld.so.cache`, `/usr/lib/x86_64-linux-gnu`) or the `LD_LIBRARY_PATH` environment variable.

---

### 1.4 Qt6 Host Environment & Platform Plugin Dependencies

- Probing Qt6 paths via `qmake6 -query`:
  - `QT_INSTALL_PREFIX`: `/usr`
  - `QT_INSTALL_PLUGINS`: `/usr/lib/x86_64-linux-gnu/qt6/plugins`
  - `QT_INSTALL_QML`: `/usr/lib/x86_64-linux-gnu/qt6/qml`
  - `QT_INSTALL_TRANSLATIONS`: `/usr/share/qt6/translations`
  - `QT_VERSION`: `6.4.2`
- Platform plugins present:
  - `platforms/libqxcb.so` (X11)
  - `platforms/libqwayland-egl.so` (Wayland EGL)
  - `platforms/libqwayland-generic.so` (Wayland generic)
  - `platforms/libqoffscreen.so` (Offscreen/headless)
- Transitive dependencies of `libqxcb.so`:
  Requires `libQt6XcbQpa.so.6`, `libxcb-icccm.so.4`, `libxcb-image.so.0`, `libxcb-keysyms.so.1`, `libxcb-randr.so.0`, `libxcb-render.so.0`, `libxcb-render-util.so.0`, `libxcb-shape.so.0`, `libxcb-shm.so.0`, `libxcb-sync.so.1`, `libxcb-xfixes.so.0`, `libxcb-xkb.so.1`, `libxcb.so.1`, `libxcb-util.so.1`, `libxkbcommon.so.0`, `libxkbcommon-x11.so.0`.
- Transitive dependencies of `libqwayland-egl.so`:
  Requires `libQt6WaylandClient.so.6`, `libQt6WaylandEglClientHwIntegration.so.6`, `libwayland-client.so.0`, `libwayland-cursor.so.0`, `libwayland-egl.so.1`.
- Plugin subdirectories required for complete Qt6 GUI functionality:
  - `platforms/`
  - `wayland-decoration-client/`
  - `wayland-graphics-integration-client/`
  - `wayland-shell-integration/`
  - `xcbglintegrations/`
  - `imageformats/` (including `libqsvg.so`)
  - `platformthemes/`
  - `platforminputcontexts/`
  - `tls/`

---

## 2. Logic Chain

### Step 1: Why the current `AppRun` causes AppImage crashes on clean machines
- Observation 1.1.A: `AppRun` runs `"$APPDIR/usr/bin/olive-editor" "$@"` directly with no `export LD_LIBRARY_PATH`.
- Observation 1.3.C: `readelf -d` shows `olive-editor` has no `DT_RPATH` or `DT_RUNPATH` and requires 23 direct shared libraries (FFmpeg, OCIO, OIIO, Qt6, OpenEXR, PortAudio).
- Deductive conclusion: When an AppImage is mounted at `/tmp/.mount_OliveXXXXXX`, the dynamic linker searches only system library paths. If the host machine does not have the exact versions of Qt6, FFmpeg 5/6, or OCIO installed in `/usr/lib`, dynamic linking fails with `error while loading shared libraries: libOpenColorIO.so.2.1: cannot open shared object file: No such file or directory`.
- Remedy: `AppRun` must prepend `${APPDIR}/usr/lib` and `${APPDIR}/usr/lib/x86_64-linux-gnu` to `LD_LIBRARY_PATH`.

### Step 2: Why Qt6 fails to initialize without `QT_PLUGIN_PATH`
- Observation 1.4: In Qt6, QPA (Qt Platform Abstraction) dynamically loads `platforms/libqxcb.so` or `platforms/libqwayland-egl.so`.
- Observation 1.1.A: `AppRun` does not export `QT_PLUGIN_PATH`.
- Deductive conclusion: Without `QT_PLUGIN_PATH="$APPDIR/usr/plugins"`, Qt6 queries host directories or hardcoded compile-time paths (`/usr/lib/x86_64-linux-gnu/qt6/plugins`). On any target system lacking identical Qt6 development packages, the application fails to start: `qt.qpa.plugin: Could not find the Qt platform plugin "xcb" in ""`.
- Remedy: `AppRun` must set `QT_PLUGIN_PATH="${APPDIR}/usr/plugins"`.

### Step 3: Why bundling Wayland alongside XCB is mandatory for modern Linux
- Observation 1.4: Both `libqxcb.so` and `libqwayland-egl.so` are present in the host Qt6 environment.
- Logic: Ubuntu (22.04+), Fedora (36+), Mint (22+), and Arch default to Wayland desktop sessions. If an AppImage only bundles X11/XCB, Wayland users suffer from fractional scaling blurriness and require XWayland emulation. Bundling both `platforms/libqxcb.so` and `platforms/libqwayland-egl.so` with their dependent plugins enables seamless native Wayland operation while maintaining full fallback compatibility with X11.

### Step 4: Elimination of the unconditional 5-second sleep in `AppRun`
- Observation 1.1.A (lines 10-17): `sleep 5` runs unconditionally on every execution, and `ps -aux | grep olive-crashhandler` is used to wait for a crash handler that is not even compiled in default builds (`app/CMakeLists.txt:146`).
- Logic: Running CLI commands (e.g. `olive-editor --version` or headless `--export`) must be instantaneous. Furthermore, `exec "$APPDIR/usr/bin/olive-editor" "$@"` replaces the shell process with the application process, allowing the OS to manage process lifecycle and return the exact exit code of `olive-editor` to the caller. If crash handler waiting is retained for crashpad-enabled builds, it must only run if `olive-crashhandler` exists in `usr/bin/` AND is actually running.

### Step 5: Resolution of AppStream and Desktop Integration Defects
- Observation 1.2.D: `appstreamcli validate` failed with 10 errors.
- Logic: Modern Linux software centers and AppImage catalog generators (AppImageHub, Flathub) reject packages with invalid AppStream metadata. Adding the missing `<p>` tags, `<launchable type="desktop-id">`, and updating categories and keywords fixes all validation failures.
- Observation 1.2.B: AppImage specification requires `AppDir/org.olivevideoeditor.Olive.desktop`, `AppDir/org.olivevideoeditor.Olive.png`, and `AppDir/.DirIcon` at the root of the bundle.
- Logic: `cmake --install` stages files inside `usr/share/...`. The packaging script must generate top-level symlinks to satisfy the AppImage runtime specification.

---

## 3. Caveats

1. **`appimagetool` Availability**:
   - The host system currently lacks `appimagetool` in its default system PATH.
   - The bundling script `packaging/linux/build_appimage.sh` must be designed so that:
     - It creates and populates the complete, portable `AppDir` staging directory.
     - If `appimagetool` is installed in `PATH` or provided as an environment variable (`APPIMAGETOOL`), it builds the final `.AppImage`.
     - If `appimagetool` is absent, it prints an informative message and leaves the fully functional `AppDir` ready for inspection or downstream packaging.
2. **Graphics Driver Isolation (The "Blacklist" Rule)**:
   - Low-level graphics and core C runtime libraries MUST NOT be bundled into `usr/lib`:
     - `libc.so.*`, `libm.so.*`, `libpthread.so.*`, `libdl.so.*`, `librt.so.*`, `libresolv.so.*`
     - `libGL.so.*`, `libGLX.so.*`, `libEGL.so.*`, `libOpenGL.so.*`, `libdrm.so.*`, `libgbm.so.*`
     - `libX11.so.*`, `libasound.so.*`
   - Bundling these leads to segmentation faults when host NVIDIA or Mesa GPU drivers attempt to communicate with conflicting driver versions inside the bundle.
3. **Flatpak Manifest Coordination**:
   - Peer agent `teamwork_preview_spec_miner_m5_3` has synthesized the Flatpak manifest targeting `org.kde.Platform` 6.8+. Flatpak builds in a containerized sandbox with its own runtime libraries, whereas AppImage bundles host-built binaries. Both require matching desktop metadata and CMake install targets.

---

## 4. Conclusion & Recommended Specifications

### 4.1 Modernized `app/packaging/linux/AppRun` Specification
Proposed replacement for `app/packaging/linux/AppRun`:
```bash
#!/usr/bin/env bash
set -e

# Resolve bundle root
HERE="$(dirname "$(readlink -f "${0}")")"
export APPDIR="${HERE}"

# Configure library search paths (bundled libs take precedence)
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Configure Qt6 runtime environment
export QT_PLUGIN_PATH="${HERE}/usr/plugins"
export QML2_IMPORT_PATH="${HERE}/usr/qml"
export QT_QPA_PLATFORMTHEME="${QT_QPA_PLATFORMTHEME:-xdgdesktopportal}"

# Configure XDG data paths for icons, mime types, and translations
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

# Execute Olive Editor directly, preserving exit status
if [[ -x "${HERE}/usr/bin/olive-crashhandler" ]]; then
  "${HERE}/usr/bin/olive-editor" "$@"
  EXIT_CODE=$?
  # Only wait if crashhandler was actually spawned
  while pgrep -x olive-crashhandler >/dev/null 2>&1; do
    sleep 1
  done
  exit "${EXIT_CODE}"
else
  exec "${HERE}/usr/bin/olive-editor" "$@"
fi
```

### 4.2 Reproducible Packaging Script: `packaging/linux/build_appimage.sh`
Proposed design for `packaging/linux/build_appimage.sh`:
1. **Inputs**: Takes build directory path (default: `build-linux-release`), output directory (default: `dist`), and optional prefix.
2. **Pre-flight Checks**: Asserts `olive-editor` binary exists in `<build-dir>/app/olive-editor`.
3. **Staging (`cmake --install`)**: Executes `cmake --install "<build-dir>" --prefix "<appdir>/usr"`.
4. **Desktop Root Integration**:
   - `ln -sf usr/share/applications/org.olivevideoeditor.Olive.desktop "${APPDIR}/"`
   - `ln -sf usr/share/icons/hicolor/256x256/apps/org.olivevideoeditor.Olive.png "${APPDIR}/"`
   - `ln -sf org.olivevideoeditor.Olive.png "${APPDIR}/.DirIcon"`
   - Copy modernized `AppRun` to `${APPDIR}/AppRun` and `chmod +x`.
5. **Qt6 Plugins Deployment**:
   - Queries `QT_PLUGIN_DIR` via `qmake6 -query QT_INSTALL_PLUGINS`.
   - Copies `platforms/`, `imageformats/`, `platformthemes/`, `platforminputcontexts/`, `xcbglintegrations/`, `wayland-decoration-client/`, `wayland-graphics-integration-client/`, `wayland-shell-integration/`, and `tls/` into `${APPDIR}/usr/plugins/`.
6. **Shared Library Bundling & Exclusion Filter**:
   - Iterates through `olive-editor` and all `.so` plugin binaries using `ldd`.
   - Copies resolved shared libraries into `${APPDIR}/usr/lib/`.
   - Explicitly skips blacklisted system libraries (`libc`, `libm`, `libpthread`, `libGL`, `libdrm`, etc.).
7. **AppImage Packaging**:
   - If `appimagetool` is available: executes `appimagetool "${APPDIR}" "${OUTPUT_DIR}/Olive-x86_64.AppImage"`.
   - If not available: logs guidance for running `appimagetool`.

### 4.3 Desktop & AppStream Metadata Enhancements
1. In `app/packaging/linux/org.olivevideoeditor.Olive.desktop`:
   - Add `AudioVideoEditing;Video;` to `Categories`.
   - Add `Keywords=video;editor;audio;non-linear;nle;cutting;timeline;`.
   - Add `StartupWMClass=olive-editor`.
2. In `app/packaging/linux/org.olivevideoeditor.Olive.xml`:
   - Add `<glob pattern="*.ovexml"/>`.
3. In `app/packaging/linux/org.olivevideoeditor.Olive.appdata.xml.in`:
   - Wrap localized descriptions in `<p>` tags.
   - Add `<launchable type="desktop-id">org.olivevideoeditor.Olive.desktop</launchable>`.
   - Update `<developer id="org.olivevideoeditor"><name>Olive Team</name></developer>`.

---

## 5. Verification Method

### 5.1 Syntax & Structural Validation
```bash
# 1. Validate bash syntax of AppRun and build script
bash -n app/packaging/linux/AppRun
bash -n packaging/linux/build_appimage.sh

# 2. Validate desktop file
desktop-file-validate app/packaging/linux/org.olivevideoeditor.Olive.desktop

# 3. Validate AppStream metadata
appstreamcli validate build-linux-release/app/packaging/linux/org.olivevideoeditor.Olive.appdata.xml

# 4. Validate MIME XML
xmllint --noout app/packaging/linux/org.olivevideoeditor.Olive.xml
```

### 5.2 Dry-Run Installation & Staging Check
```bash
# Verify CMake install targets execute cleanly without error
cmake --install build-linux-release --prefix /tmp/olive-verify-staging
test -x /tmp/olive-verify-staging/bin/olive-editor
test -f /tmp/olive-verify-staging/share/applications/org.olivevideoeditor.Olive.desktop
test -f /tmp/olive-verify-staging/share/icons/hicolor/256x256/apps/org.olivevideoeditor.Olive.png
rm -rf /tmp/olive-verify-staging
```

### 5.3 Invalidation Conditions
- Any CMake configure error when evaluating `app/packaging/linux/CMakeLists.txt`.
- Failure of `readelf -d` on `olive-editor` to resolve dynamic libraries when `LD_LIBRARY_PATH` is pointed to the bundle `usr/lib`.
- Inability of Qt6 to load `platforms/libqxcb.so` due to missing `libQt6XcbQpa.so.6`.
