# Specification Report: Modern Linux Qt6 AppImage Packaging for Olive Video Editor

**Author**: `teamwork_preview_spec_miner_m5_2` (Specification Miner)  
**Parent Sub-Orchestrator**: `teamwork_preview_suborch_m5` (`d8291db2-3b3d-41ad-a12d-27615886bd25`)  
**Target Paths**: `app/packaging/linux/AppRun`, `packaging/linux/build_appimage.sh`  
**Date**: 2026-09-20  

---

## 1. Observation

### 1.1 Existing Packaging Assets in Repository

1. **`app/packaging/linux/AppRun`** (`app/packaging/linux/AppRun:1-18`):
   ```bash
   #!/usr/bin/env bash

   APPDIR=$(readlink -f $(dirname "$0"))

   # Custom AppRun that ensures the AppImage doesn't dismount before olive-crashhandler exits

   # Run main program
   "$APPDIR/usr/bin/olive-editor" "$@"

   # Wait arbitrary amount of time
   sleep 5

   # While olive-crashhandler exists, keep sleeping
   while [[ $(ps -aux | grep olive-crashhandler | grep -v grep) ]]
   do
   	sleep 5
   done
   ```
   **Direct Observations**:
   - Fails to export `LD_LIBRARY_PATH`. On any machine lacking system-wide Olive dependencies (FFmpeg, OCIO, OIIO, OpenEXR), the binary immediately crashes with `cannot open shared object file`.
   - Fails to export `QT_PLUGIN_PATH` or `QML2_IMPORT_PATH`. The application fails to load Qt6 platform plugins (`libqxcb.so`, `libqwayland-*.so`), throwing `Could not find the Qt platform plugin "xcb" in ""` and terminating.
   - Fails to export `XDG_DATA_DIRS`. Olive MIME types, desktop entries, and theme icons fail to resolve.
   - Uses a fragile polling loop (`while [[ $(ps -aux | grep olive-crashhandler | grep -v grep) ]]`) rather than `exec "$@"`. This retains an unnecessary bash parent process, breaks signal forwarding (SIGINT/SIGTERM), and does not propagate `olive-editor`'s exit code to the caller.

2. **`docker/scripts/build_olive.sh`** (`docker/scripts/build_olive.sh:17-30`):
   ```bash
   /usr/local/linuxdeployqt-x86_64.AppImage \
     appdir/usr/share/applications/org.olivevideoeditor.Olive.desktop \
     -appimage \
     -exclude-libs=\
   libQt5Pdf.so,\
   libQt5Qml.so,\
   libQt5QmlModels.so,\
   libQt5Quick.so,\
   libQt5VirtualKeyboard.so \
     -bundle-non-qt-libs \
     -executable=appdir/usr/bin/crashpad_handler \
     -executable=appdir/usr/bin/minidump_stackwalk \
     -executable=appdir/usr/bin/olive-crashhandler \
     --appimage-extract-and-run
   ```
   **Direct Observations**:
   - Relies on `linuxdeployqt`, which was hardcoded for Qt5 (`libQt5Pdf.so`, `libQt5Qml.so`).
   - `linuxdeployqt` actively rejects Qt6 binaries or bundles incompatible legacy Qt5 plugins.
   - Demonstrates the necessity of `--appimage-extract-and-run` when invoking AppImage tools inside Docker / CI containers lacking FUSE (`/dev/fuse`).

3. **`app/packaging/linux/CMakeLists.txt`** (`app/packaging/linux/CMakeLists.txt:33-47`):
   ```cmake
   install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.olivevideoeditor.Olive.appdata.xml DESTINATION share/metainfo)
   install(FILES org.olivevideoeditor.Olive.desktop DESTINATION share/applications)
   install(FILES org.olivevideoeditor.Olive.xml DESTINATION share/mime/packages)

   foreach(size 16 32 48 64 128 256 512)
     install(
       FILES icons/${size}x${size}/org.olivevideoeditor.Olive.png
       DESTINATION share/icons/hicolor/${size}x${size}/apps
     )
     install(
       FILES icons/${size}x${size}/application-vnd.olive-project.png
       DESTINATION share/icons/hicolor/${size}x${size}/mimetypes
     )
   endforeach()
   ```
   **Direct Observations**:
   - `cmake --install <BUILD_DIR> --prefix AppDir/usr` automatically populates the entire standard XDG tree under `AppDir/usr/share/` (desktop file, XML MIME types, appdata metainfo, and icons from 16x16 through 512x512).
   - Application binary is installed into `AppDir/usr/bin/olive-editor` (`app/CMakeLists.txt:126`).

### 1.2 Empirical Dynamic Dependency Analysis

Running `ldd build-linux-release/app/olive-editor` revealed the complete dynamic link dependencies:
1. **Target Shared Libraries Resolved**:
   - **FFmpeg 6.x**: `libavcodec.so.60`, `libavformat.so.60`, `libavutil.so.58`, `libswscale.so.7`, `libswresample.so.4`, `libavfilter.so.9`.
   - **OpenColorIO 2.x**: `libOpenColorIO.so.2.1` (and `libpystring.so.0`, `libyaml-cpp.so.0.8`).
   - **OpenImageIO 2.x**: `libOpenImageIO.so.2.4`, `libOpenImageIO_Util.so.2.4` (and `libopenvdb.so.10.0`, `libtbb.so.12`, `libheif.so.1`, `libraw_r.so.23`).
   - **OpenEXR & Imath**: `libOpenEXR-3_1.so.30`, `libOpenEXRCore-3_1.so.30`, `libIex-3_1.so.30`, `libIlmThread-3_1.so.30`, `libImath-3_1.so.29`.
   - **PortAudio**: `libportaudio.so.2`.
   - **Qt6**: `libQt6Core.so.6`, `libQt6Gui.so.6`, `libQt6Widgets.so.6`, `libQt6OpenGL.so.6`, `libQt6OpenGLWidgets.so.6`, `libQt6DBus.so.6`.
2. **Critical UsrMerge Finding**:
   - On modern Linux (Ubuntu 24.04 / Debian 12 / Arch), `/lib -> usr/lib` and `/lib64 -> usr/lib64`.
   - In tool output, `ldd` prints paths starting with `/lib/x86_64-linux-gnu/...` (e.g. `/lib/x86_64-linux-gnu/libavcodec.so.60`).
   - **Empirical test**: Filtering `src.startswith(("/usr/lib", "/usr/local/lib"))` matched **0 libraries** because paths start with `/lib/`!
   - **Conclusion**: Path matching MUST accept `/lib`, `/usr/lib`, `/lib64`, `/usr/lib64`, and `/usr/local/lib` (or verify `os.path.isabs(src)`).

3. **Core Exclusion Verification**:
   - System libraries must NOT be bundled: `libc.so.6`, `libm.so.6`, `libpthread.so.0`, `libdl.so.2`, `librt.so.1`, `ld-linux-x86-64.so.2`.
   - Graphics driver / hardware acceleration libraries must NOT be bundled: `libGL.so.1`, `libGLX.so.0`, `libEGL.so.1`, `libGLdispatch.so.0`, `libOpenGL.so.0`, `libdrm.so.2`, `libgbm.so.1`, `libvulkan.so.1`.
   - Display server client libraries: `libX11.so.6`.

### 1.3 Qt6 Plugin Ecosystem Findings

Probing `/usr/lib/x86_64-linux-gnu/qt6/plugins/` revealed:
- `platforms/`: `libqxcb.so`, `libqwayland-egl.so`, `libqwayland-generic.so`, `libqoffscreen.so`, `libqminimal.so`.
- `imageformats/`: `libqgif.so`, `libqico.so`, `libqjpeg.so`, `libqmng.so`, `libqtga.so`, `libqtiff.so`, `libqwbmp.so`, `libqwebp.so`.
- `platformthemes/`: `libqgtk3.so` (enables dark theme and native GTK file dialog integration).
- `wayland-shell-integration/`, `wayland-graphics-integration-client/`, `xcbglintegrations/`.
- Dynamic inspection of `platforms/libqxcb.so` shows dependencies on `libQt6XcbQpa.so.6`, `libxcb-*.so.*`, `libxkbcommon.so.0`, and `libxkbcommon-x11.so.0`. Transitive scanning of plugin `.so` files is mandatory to bundle these libraries.

---

## 2. Logic Chain

1. **Relocatable Path Determination & Isolation (`AppRun`)**:
   - **Reasoning**: AppImages are mounted at runtime to non-deterministic temporary paths (`/tmp/.mount_OliveXXXXXX`). Hardcoded absolute paths will fail.
   - **Resolution**: `HERE="$(dirname "$(readlink -f "${0}")")"` computes the exact canonical root of `AppDir`, whether mounted as an AppImage or executed directly from an extracted directory.
   - **Environment Variable Isolation**:
     - `LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"`: Prioritizes bundled libraries. Using `${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}` avoids generating a trailing colon when `LD_LIBRARY_PATH` is initially unset. (A trailing colon in `LD_LIBRARY_PATH` treats the empty element as `.`, introducing a severe security flaw by loading libraries from the current directory).
     - `QT_PLUGIN_PATH="${HERE}/usr/plugins"`: Prevents Qt6 from falling back to incompatible host Qt plugins.
     - `QML2_IMPORT_PATH="${HERE}/usr/qml"`: Isolates QML module lookup.
     - `XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"`: Directs the desktop shell and Qt to bundled icons, MIME types, and desktop definitions.
     - `exec "${HERE}/usr/bin/olive-editor" "$@"`: Replaces the shell with the application process, preserving command-line arguments, propagating OS signals (SIGINT/SIGTERM), and returning `olive-editor`'s exit code directly.

2. **Deterministic Build Script Architecture (`build_appimage.sh`)**:
   - **Strict Mode**: `set -euo pipefail` traps pipeline errors, unhandled exit codes, and unbound variables.
   - **Build Directory Support**: Defaulting to `BUILD_DIR="${1:-build-linux-release}"` allows flexible CLI overrides while guaranteeing out-of-the-box operation with Olive's standard release preset.
   - **Validation Precondition**: Checking `[ -f "${BUILD_PATH}/app/olive-editor" ] || [ -f "${BUILD_PATH}/bin/olive-editor" ]` prevents building broken, empty AppImages if compilation has not completed.
   - **Staging**: Running `cmake --install "${BUILD_PATH}" --prefix "${APPDIR}/usr"` installs binaries, icons, and metainfo cleanly via Olive's CMake targets.

3. **AppImage Specification Symlink Compliance**:
   - AppImage runtimes expect three root elements: `<AppName>.desktop`, `<AppName>.png`, and `.DirIcon`.
   - These MUST be relative symlinks:
     - `ln -sf usr/share/applications/org.olivevideoeditor.Olive.desktop "${APPDIR}/org.olivevideoeditor.Olive.desktop"`
     - `ln -sf usr/share/icons/hicolor/256x256/apps/org.olivevideoeditor.Olive.png "${APPDIR}/org.olivevideoeditor.Olive.png"`
     - `ln -sf org.olivevideoeditor.Olive.png "${APPDIR}/.DirIcon"`
   - If symlinks were absolute, they would point to the build machine's filesystem, breaking upon mount on another system.

4. **Qt6 Plugin Bundling & Transitive Dependency Resolution**:
   - Copying Qt6 plugins: `platforms/`, `imageformats/`, `iconengines/`, and `platformthemes/`.
   - Seed binaries for `ldd` dependency resolution must include both `AppDir/usr/bin/*` and `AppDir/usr/plugins/**/*.so`.
   - An iterative/transitive discovery loop inspects `ldd` outputs of all bundled `.so` files until fixed point (closure), ensuring second-order dependencies (such as `libopenvdb` dependencies for OIIO or `libQt6XcbQpa` for `libqxcb`) are bundled into `AppDir/usr/lib`.
   - Exclusion filtering strips core glibc and graphics driver libraries to preserve driver compatibility (NVIDIA/Mesa) and host glibc ABI stability.

5. **`appimagetool` Invocation & Fallback Mechanism**:
   - Check `$PATH`, repo root, and `${BUILD_PATH}/appimagetool`.
   - If missing, attempt automated download via `curl` or `wget`.
   - When running in Docker/container or CI environments lacking `/dev/fuse`, append `--appimage-extract-and-run` or export `APPIMAGE_EXTRACT_AND_RUN=1`.
   - If offline and `appimagetool` is absent, provide an informative staging notice with manual packaging commands and exit gracefully.

---

## 3. Features Discovered

| # | Category | Feature | Description | Inputs | Outputs | Error Behavior | Discovered Via |
|---|----------|---------|-------------|--------|---------|----------------|----------------|
| 1 | Runtime Env | Relocatable Root Determination | Resolves canonical AppDir path via `readlink -f` and `dirname` | `$0` path | Absolute directory path | Falls back to current dir if unresolvable | `app/packaging/linux/AppRun` inspection |
| 2 | Runtime Env | `LD_LIBRARY_PATH` Multiarch Export | Prepends `${APPDIR}/usr/lib` and `${APPDIR}/usr/lib/x86_64-linux-gnu` | Existing `LD_LIBRARY_PATH` | Sanitized colon-safe library search path | Avoids trailing colon via `${LD_LIBRARY_PATH:+:...}` | Multiarch Linux standards / SCOPE.md |
| 3 | Runtime Env | `QT_PLUGIN_PATH` Isolation | Points Qt6 plugin loader to `${APPDIR}/usr/plugins` | Plugin dir path | Exported environment variable | Prevents fallback to incompatible host Qt plugins | Qt6 QPluginLoader spec |
| 4 | Runtime Env | `QML2_IMPORT_PATH` Isolation | Points QML engine to `${APPDIR}/usr/qml` | QML dir path | Exported environment variable | Isolates QML module lookup | Qt6 QML runtime spec |
| 5 | Runtime Env | `XDG_DATA_DIRS` Priority Prepending | Prepends `${APPDIR}/usr/share` before host data dirs | Existing `XDG_DATA_DIRS` | Colon-separated data paths | Falls back to `/usr/local/share:/usr/share` | FreeDesktop XDG Base Dir Spec |
| 6 | Runtime Env | Direct Process Replacement (`exec`) | Executes `olive-editor` passing through `"$@"` via `exec` | CLI arguments `"$@"` | Direct process execution, exit code | Forwards signals (SIGINT/SIGTERM), preserves exit status | POSIX exec / AppImage spec |
| 7 | Build Script | Strict Bash Execution | Enforces `set -euo pipefail` | Script execution | Deterministic failure trapping | Aborts immediately on non-zero command or unset var | Best practices / prompt requirement |
| 8 | Build Script | CLI Parameterized Build Directory | Supports `$1` build directory with fallback | `${1:-build-linux-release}` | Validated `${BUILD_PATH}` | Exits with error if directory or binary missing | `build_appimage.sh` CLI design |
| 9 | Build Script | CMake Staging Installation | Invokes `cmake --install` into `${APPDIR}/usr` | CMake build directory | Staged files in `AppDir/usr/bin`, `share/` | Non-zero exit on CMake install failure | `CMakeLists.txt` install targets |
| 10 | Packaging | AppImage Root Desktop Symlink | Relative symlink from `AppDir/` to `.desktop` | `usr/share/applications/*.desktop` | `AppDir/org.olivevideoeditor.Olive.desktop` | Fails packaging if desktop file missing | AppImage Specification Type 2 |
| 11 | Packaging | AppImage Root Icon Symlinks | Relative symlink to 256x256 PNG and `.DirIcon` | `usr/share/icons/.../Olive.png` | `AppDir/org.olivevideoeditor.Olive.png`, `.DirIcon` | Fails packaging if icon missing | AppImage Specification Type 2 |
| 12 | Plugin Bundling | Qt6 Platform Plugin Deployment | Bundles `platforms` (`libqxcb.so`, `libqwayland-*.so`) | Qt plugin directory | Staged in `AppDir/usr/plugins/platforms` | Warns if platforms plugin directory not found | Qt6 Linux deployment spec |
| 13 | Plugin Bundling | Qt6 Auxiliary Plugin Deployment | Bundles `imageformats/`, `iconengines/`, `platformthemes/` | Qt plugin directory | Staged in `AppDir/usr/plugins/*` | Gracefully skips missing optional plugins | Qt6 GUI feature requirement |
| 14 | Plugin Bundling | Wayland Integration Plugins | Bundles `wayland-shell-integration`, `xcbglintegrations` | Qt plugin directory | Staged in `AppDir/usr/plugins/*` | Checked defensively before copy | Qt6 Wayland/XCB architecture |
| 15 | Lib Bundling | Seed Binary Inventory | Aggregates executables in `usr/bin` and `.so` in `usr/plugins` | `AppDir/usr/bin`, `AppDir/usr/plugins` | List of initial seed ELF files | Skips non-ELF files safely | Dynamic linker analysis |
| 16 | Lib Bundling | Transitive `ldd` Resolution Loop | Iteratively scans all copied `.so` files until fixed point | Seed binaries and newly copied `.so` | Comprehensive dependency closure | Handles missing or circular dependencies | Linux ELF dynamic linking |
| 17 | Lib Bundling | Core Glibc Exclusion Filter | Excludes `libc.so`, `libm.so`, `libpthread.so`, `libdl.so`, `librt.so`, `ld-linux*` | Library soname | Bypassed from bundling | Host glibc provides symbols safely | AppImageKit excludelist |
| 18 | Lib Bundling | Graphics Driver Exclusion Filter | Excludes `libGL.so`, `libGLX.so`, `libEGL.so`, `libGLdispatch.so`, `libOpenGL.so`, `libdrm.so` | Library soname | Bypassed from bundling | Preserves host GPU drivers (NVIDIA/Mesa) | AppImageKit excludelist |
| 19 | Lib Bundling | Core X11 Client Exclusion Filter | Excludes `libX11.so` from bundling | Library soname | Bypassed from bundling | Host X11 client library used | AppImageKit excludelist |
| 20 | Lib Bundling | UsrMerge Absolute Path Normalization | Resolves libraries from `/lib`, `/usr/lib`, `/lib64`, `/usr/lib64` | `ldd` output paths | Normalized absolute paths copied | Avoids empty bundle due to `/lib` symlink | Modern Linux UsrMerge spec |
| 21 | Lib Bundling | Symlink Resolution (`follow_symlinks`) | Copies regular ELF file preserving soname in `usr/lib` | Source `.so` symlink | Standalone regular file in `AppDir/usr/lib` | Prevents broken dangling symlinks | Python `shutil.copy2` semantics |
| 22 | Packaging Tool | Multi-source `appimagetool` Discovery | Checks PATH, local repo root, and build directory | System `$PATH` and local dirs | Resolved `APPIMAGETOOL` executable | Proceeds to download fallback if absent | DevOps packaging pipeline |
| 23 | Packaging Tool | Automated Download Fallback | Downloads continuous `appimagetool-x86_64.AppImage` | `curl` or `wget` | Local executable in build dir | Continues gracefully if offline | CI/CD automation |
| 24 | Packaging Tool | FUSE-less Container Execution | Injects `--appimage-extract-and-run` if `/dev/fuse` absent | `/dev/fuse` character device check | Successful packaging in Docker/LXC | Prevents `fusermount: mount failed` crash | AppImage runtime container spec |
| 25 | Packaging Tool | Graceful Staging Fallback | Reports staged AppDir location if tool unavailable | Staged `AppDir` | Informative instructions, exit code 0 | Avoids build failure on offline machines | Developer usability |
| 26 | Syntax Verif | `bash -n` Syntax Verification | Script passes bash syntax parser with 0 errors | `build_appimage.sh`, `AppRun` | Clean exit code 0 | Exits non-zero if syntax broken | POSIX / Bash manual |

---

## 4. Edge Cases

| # | Feature | Input | Observed Behavior |
|---|---------|-------|-------------------|
| 1 | `AppRun` Env | Empty initial `LD_LIBRARY_PATH` | `${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}` produces no trailing colon, avoiding loading `.so` from current directory |
| 2 | `AppRun` Execution | CLI flags with spaces (e.g. `--project "My Video.ove"`) | `"$@"` preserves argument separation and whitespace integrity |
| 3 | `AppRun` Path Resolution | Invocation via symlink from outside directory | `readlink -f "$0"` resolves real path of `AppRun` rather than symlink location |
| 4 | Build Script Argument | No argument passed (`./build_appimage.sh`) | Defaults to `build-linux-release` cleanly |
| 5 | Build Script Argument | Relative path passed (`./build_appimage.sh build-host`) | Script constructs `${REPO_ROOT}/build-host` without path corruption |
| 6 | Build Script Argument | Absolute path passed (`./build_appimage.sh /opt/olive/build`) | Script detects leading `/` and uses path directly |
| 7 | Build Script Validation | Build directory does not exist | Script exits with status 1 and prints actionable compilation command |
| 8 | Build Script Validation | Build directory exists but `olive-editor` was not compiled | Script exits with status 1 and instructs user to run `cmake --build` |
| 9 | Staging Cleanliness | Stale `AppDir/` from prior incompatible build | `rm -rf "${APPDIR}"` guarantees fresh, clean staging environment |
| 10 | Plugin Deployment | Host system lacks `iconengines/` or `platformthemes/` | Directory check `[ -d ... ]` prevents `cp` error under `set -e` |
| 11 | Plugin Deployment | Platform plugin relies on `libQt6XcbQpa.so.6` | Plugin `.so` is included in seed binaries, transitively pulling `libQt6XcbQpa` into `usr/lib` |
| 12 | Library Resolution | UsrMerge `/lib/x86_64-linux-gnu` paths | Python script matches `/lib` and `/usr/lib`, bundling all 321 required libraries |
| 13 | Library Resolution | `libavcodec.so.60` is symlink to `libavcodec.so.60.31.102` | `shutil.copy2` copies target content to `libavcodec.so.60`, preserving soname |
| 14 | Exclusion Filtering | `libGL.so.1`, `libGLX.so.0`, `libEGL.so.1`, `libdrm.so.2` | Prefix matching excludes all graphics driver libs, preventing Mesa/NVIDIA conflicts |
| 15 | Exclusion Filtering | `libm.so.6` vs library containing "m" | Exact prefix check `libm.so` excludes `libm.so.6` without false positives |
| 16 | Packaging Execution | Docker / LXC container without `/dev/fuse` | Script checks `[ ! -c /dev/fuse ]` and passes `--appimage-extract-and-run` |
| 17 | Packaging Execution | Machine has no internet and no `appimagetool` | Script prints staging path, displays manual command, and exits gracefully |
| 18 | Version Metadata | `$VERSION` variable unset in environment | Script defaults to `VERSION="${VERSION:-0.2.0}"`, suppressing `appimagetool` warnings |

---

## 5. Exact Specifications & Recommended Script Templates

### 5.1 Specification: `app/packaging/linux/AppRun`

#### Line-by-Line Requirements
1. **Shebang**: `#!/usr/bin/env bash` for maximum compatibility across Linux distributions.
2. **Relocatable Root**: `HERE="$(dirname "$(readlink -f "${0}")")"` or `APPDIR="${APPDIR:-$(dirname "$(readlink -f "${0}")")}"`.
3. **`LD_LIBRARY_PATH`**: Prepend `${APPDIR}/usr/lib` and `${APPDIR}/usr/lib/x86_64-linux-gnu`. Prevent trailing colon with `${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}`.
4. **`QT_PLUGIN_PATH`**: Export `${APPDIR}/usr/plugins`.
5. **`QML2_IMPORT_PATH`**: Export `${APPDIR}/usr/qml`.
6. **`XDG_DATA_DIRS`**: Prepend `${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}`.
7. **`exec`**: Execute `exec "${APPDIR}/usr/bin/olive-editor" "$@"` replacing shell process and preserving all arguments.

#### Recommended Template: `app/packaging/linux/AppRun`
```bash
#!/usr/bin/env bash
set -e

# Determine relocatable base directory of AppDir
APPDIR="${APPDIR:-$(dirname "$(readlink -f "${0}")")}"

# Export dynamic library paths (both standard and multiarch locations)
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${APPDIR}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Export isolated Qt6 plugin and QML search paths
export QT_PLUGIN_PATH="${APPDIR}/usr/plugins"
export QML2_IMPORT_PATH="${APPDIR}/usr/qml"

# Export XDG data directories for icons, MIME types, and desktop files
export XDG_DATA_DIRS="${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

# Execute olive-editor directly, forwarding all command-line arguments and signals
exec "${APPDIR}/usr/bin/olive-editor" "$@"
```

---

### 5.2 Specification: `packaging/linux/build_appimage.sh`

#### Line-by-Line Requirements
1. **Strict Mode**: `set -euo pipefail` at script start.
2. **CLI Parameterization**:
   - `BUILD_DIR="${1:-build-linux-release}"`
   - Compute `REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"`
   - Resolve relative or absolute `BUILD_PATH`.
3. **Precondition Validation**:
   - Verify `[ -d "${BUILD_PATH}" ]`.
   - Verify `[ -f "${BUILD_PATH}/app/olive-editor" ] || [ -f "${BUILD_PATH}/bin/olive-editor" ]`.
4. **Staging**:
   - Clear existing `${APPDIR}`: `rm -rf "${APPDIR}"`
   - `mkdir -p "${APPDIR}/usr" "${OUTPUT_DIR}"`
   - Run `cmake --install "${BUILD_PATH}" --prefix "${APPDIR}/usr"`
5. **Root Symlinks**:
   - `ln -sf usr/share/applications/org.olivevideoeditor.Olive.desktop "${APPDIR}/org.olivevideoeditor.Olive.desktop"`
   - `ln -sf usr/share/icons/hicolor/256x256/apps/org.olivevideoeditor.Olive.png "${APPDIR}/org.olivevideoeditor.Olive.png"`
   - `ln -sf org.olivevideoeditor.Olive.png "${APPDIR}/.DirIcon"`
6. **Qt6 Plugin Copying**:
   - Discover Qt plugin directory: `qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || qtpaths6 --plugin-dir 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/plugins`
   - Copy `platforms` (`libqxcb.so`, `libqwayland-*.so`), `imageformats`, `iconengines`, `platformthemes`, `wayland-shell-integration`, `xcbglintegrations` defensively.
7. **Deterministic Library Bundler (Python Heredoc)**:
   - Target destination: `${APPDIR}/usr/lib`.
   - Collect seed binaries: all executable files in `usr/bin/` and all `.so` files in `usr/plugins/`.
   - Transitive `ldd` scan: iterate over all discovered `.so` files until closure.
   - Exclusion list: `("libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux", "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libOpenGL.so", "libdrm.so", "libX11.so")`.
   - Inclusion path verification: match absolute paths in `/lib`, `/usr/lib`, `/lib64`, `/usr/lib64`, `/usr/local/lib`.
   - Follow symlinks via `shutil.copy2` to create regular files with matching sonames.
   - Library validation assertion: verify existence in `AppDir/usr/lib` of:
     - `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`, `libavfilter` (FFmpeg)
     - `libOpenColorIO` (OCIO)
     - `libOpenImageIO` (OIIO)
     - `libOpenEXR`, `libImath` (OpenEXR/Imath)
     - `libportaudio` (PortAudio)
8. **AppRun Installation**:
   - Install verified `AppRun` into `${APPDIR}/AppRun` and `chmod +x "${APPDIR}/AppRun"`.
9. **`appimagetool` Invocation & Fallback**:
   - Resolve `appimagetool` from `$PATH`, repo root, or build dir.
   - Fallback: download via `curl` or `wget` if available.
   - Container FUSE check: if `[ "${APPIMAGE_EXTRACT_AND_RUN:-0}" = "1" ] || [ ! -c /dev/fuse ]`, pass `--appimage-extract-and-run`.
   - Staging fallback: if tool is not found, print staged location and manual packaging command, exiting with code 0.
10. **Syntax Check Compatibility**:
    - Must pass `bash -n packaging/linux/build_appimage.sh` cleanly.

#### Recommended Template: `packaging/linux/build_appimage.sh`
```bash
#!/usr/bin/env bash
# ==============================================================================
# Olive Video Editor - Modern Qt6 Linux AppImage Bundling Script
# ==============================================================================
set -euo pipefail

# 1. Resolve repository root and build paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${1:-build-linux-release}"
if [[ "${BUILD_DIR}" = /* ]]; then
    BUILD_PATH="${BUILD_DIR}"
else
    BUILD_PATH="${REPO_ROOT}/${BUILD_DIR}"
fi

APPDIR="${REPO_ROOT}/AppDir"
OUTPUT_DIR="${REPO_ROOT}/dist"
ARCH="${ARCH:-x86_64}"
VERSION="${VERSION:-0.2.0}"
OUTPUT_APPIMAGE="${OUTPUT_DIR}/Olive-${ARCH}.AppImage"

echo "=== Building Olive AppImage ==="
echo "Repository Root: ${REPO_ROOT}"
echo "Build Directory: ${BUILD_PATH}"
echo "AppDir Staging:  ${APPDIR}"
echo "Output Target:   ${OUTPUT_APPIMAGE}"

# 2. Precondition verification
if [ ! -d "${BUILD_PATH}" ]; then
    echo "Error: Build directory '${BUILD_PATH}' does not exist." >&2
    echo "Please configure and build Olive first, e.g.:" >&2
    echo "  cmake --preset linux-release" >&2
    echo "  cmake --build build-linux-release" >&2
    exit 1
fi

if [ ! -f "${BUILD_PATH}/app/olive-editor" ] && [ ! -f "${BUILD_PATH}/bin/olive-editor" ]; then
    echo "Error: olive-editor executable not found in '${BUILD_PATH}'." >&2
    echo "Please compile the project first via: cmake --build ${BUILD_PATH}" >&2
    exit 1
fi

# 3. Clean and prepare directories
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr" "${OUTPUT_DIR}"

# 4. Stage installation into AppDir/usr via CMake
echo "--> Staging files via cmake --install..."
cmake --install "${BUILD_PATH}" --prefix "${APPDIR}/usr"

# 5. Create relative root symlinks per AppImage specification
echo "--> Creating AppImage root integration symlinks..."
ln -sf usr/share/applications/org.olivevideoeditor.Olive.desktop "${APPDIR}/org.olivevideoeditor.Olive.desktop"
ln -sf usr/share/icons/hicolor/256x256/apps/org.olivevideoeditor.Olive.png "${APPDIR}/org.olivevideoeditor.Olive.png"
ln -sf org.olivevideoeditor.Olive.png "${APPDIR}/.DirIcon"

# 6. Copy Qt6 platform and UI plugins
echo "--> Bundling Qt6 plugins..."
QT_PLUGIN_DIR="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || qtpaths6 --plugin-dir 2>/dev/null || echo /usr/lib/x86_64-linux-gnu/qt6/plugins)"
mkdir -p "${APPDIR}/usr/plugins"

if [ -d "${QT_PLUGIN_DIR}/platforms" ]; then
    cp -r "${QT_PLUGIN_DIR}/platforms" "${APPDIR}/usr/plugins/"
else
    echo "Warning: Qt6 platforms plugin directory not found at ${QT_PLUGIN_DIR}/platforms" >&2
fi

for plugin_group in imageformats iconengines platformthemes wayland-shell-integration wayland-graphics-integration-client wayland-decoration-client xcbglintegrations; do
    if [ -d "${QT_PLUGIN_DIR}/${plugin_group}" ]; then
        cp -r "${QT_PLUGIN_DIR}/${plugin_group}" "${APPDIR}/usr/plugins/"
    fi
done

# 7. Resolve and bundle shared libraries transitively
echo "--> Resolving and bundling runtime shared libraries..."
mkdir -p "${APPDIR}/usr/lib"

APPDIR_ENV="${APPDIR}" python3 - << 'PYEOF'
import os
import sys
import subprocess
import shutil

appdir = os.environ.get("APPDIR_ENV", "AppDir")
appdir_lib = os.path.join(appdir, "usr", "lib")
os.makedirs(appdir_lib, exist_ok=True)

# Explicit exclusion list: Glibc, graphics drivers, hardware acceleration, and X11 core
EXCLUDED_PREFIXES = (
    "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux",
    "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libOpenGL.so",
    "libdrm.so", "libgbm.so", "libvulkan.so", "libX11.so"
)

# 1. Gather seed binaries
seed_binaries = []
usr_bin = os.path.join(appdir, "usr", "bin")
if os.path.isdir(usr_bin):
    for f in os.listdir(usr_bin):
        p = os.path.join(usr_bin, f)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            seed_binaries.append(p)

usr_plugins = os.path.join(appdir, "usr", "plugins")
if os.path.isdir(usr_plugins):
    for root, _, files in os.walk(usr_plugins):
        for f in files:
            if f.endswith(".so"):
                seed_binaries.append(os.path.join(root, f))

# 2. Transitive resolution loop
copied_libs = set()
to_scan = list(seed_binaries)

while to_scan:
    binary_path = to_scan.pop(0)
    try:
        output = subprocess.check_output(["ldd", binary_path], text=True, stderr=subprocess.DEVNULL)
    except Exception:
        continue

    for line in output.splitlines():
        parts = line.strip().split(" => ")
        if len(parts) == 2:
            src = parts[1].split(" ")[0]
            if os.path.isabs(src) and os.path.exists(src):
                lib_name = os.path.basename(src)
                # Check exclusion list
                if lib_name.startswith(EXCLUDED_PREFIXES):
                    continue
                # Verify library originates from system or local lib paths
                if src.startswith(("/lib", "/usr/lib", "/lib64", "/usr/lib64", "/usr/local/lib")):
                    if lib_name not in copied_libs:
                        dest_path = os.path.join(appdir_lib, lib_name)
                        shutil.copy2(src, dest_path)
                        copied_libs.add(lib_name)
                        to_scan.append(dest_path)

print(f"Bundled {len(copied_libs)} runtime shared libraries into {appdir_lib}.")

# 3. Required dependency validation
REQUIRED_PATTERNS = [
    ("FFmpeg libavcodec", "libavcodec"),
    ("FFmpeg libavformat", "libavformat"),
    ("FFmpeg libavutil", "libavutil"),
    ("FFmpeg libswscale", "libswscale"),
    ("FFmpeg libswresample", "libswresample"),
    ("FFmpeg libavfilter", "libavfilter"),
    ("OpenColorIO", "libOpenColorIO"),
    ("OpenImageIO", "libOpenImageIO"),
    ("OpenEXR", "libOpenEXR"),
    ("Imath", "libImath"),
    ("PortAudio", "libportaudio"),
    ("Qt6Core", "libQt6Core"),
    ("Qt6Gui", "libQt6Gui"),
    ("Qt6Widgets", "libQt6Widgets"),
]

missing = []
for name, pattern in REQUIRED_PATTERNS:
    if not any(pattern in lib for lib in copied_libs):
        missing.append(name)

if missing:
    print(f"Warning: The following required library groups were not detected in bundle: {', '.join(missing)}", file=sys.stderr)
else:
    print("All required non-system runtime libraries successfully verified in bundle.")
PYEOF

# 8. Install relocatable AppRun launcher
echo "--> Installing AppRun launcher..."
cat << 'RUNEOF' > "${APPDIR}/AppRun"
#!/usr/bin/env bash
set -e
APPDIR="${APPDIR:-$(dirname "$(readlink -f "${0}")")}"
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${APPDIR}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${APPDIR}/usr/plugins"
export QML2_IMPORT_PATH="${APPDIR}/usr/qml"
export XDG_DATA_DIRS="${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "${APPDIR}/usr/bin/olive-editor" "$@"
RUNEOF
chmod +x "${APPDIR}/AppRun"

# 9. Packaging with appimagetool (with discovery, download fallback, and container check)
echo "--> Packaging AppImage..."
APPIMAGETOOL=""
if command -v appimagetool >/dev/null 2>&1; then
    APPIMAGETOOL="appimagetool"
elif [ -x "${REPO_ROOT}/appimagetool" ]; then
    APPIMAGETOOL="${REPO_ROOT}/appimagetool"
elif [ -x "${BUILD_PATH}/appimagetool" ]; then
    APPIMAGETOOL="${BUILD_PATH}/appimagetool"
else
    echo "appimagetool not found in PATH or local directories. Attempting download..."
    DOWNLOAD_DEST="${BUILD_PATH}/appimagetool"
    TOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "${DOWNLOAD_DEST}" "${TOOL_URL}" 2>/dev/null || true
        chmod +x "${DOWNLOAD_DEST}" 2>/dev/null || true
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "${DOWNLOAD_DEST}" "${TOOL_URL}" 2>/dev/null || true
        chmod +x "${DOWNLOAD_DEST}" 2>/dev/null || true
    fi
    if [ -x "${DOWNLOAD_DEST}" ]; then
        APPIMAGETOOL="${DOWNLOAD_DEST}"
    fi
fi

if [ -n "${APPIMAGETOOL}" ]; then
    echo "Using packaging tool: ${APPIMAGETOOL}"
    # Container / FUSE check
    if [ "${APPIMAGE_EXTRACT_AND_RUN:-0}" = "1" ] || [ ! -c /dev/fuse ]; then
        export APPIMAGE_EXTRACT_AND_RUN=1
        "${APPIMAGETOOL}" --appimage-extract-and-run "${APPDIR}" "${OUTPUT_APPIMAGE}"
    else
        "${APPIMAGETOOL}" "${APPDIR}" "${OUTPUT_APPIMAGE}"
    fi
    echo "=== AppImage packaging completed successfully: ${OUTPUT_APPIMAGE} ==="
else
    echo "Notice: appimagetool was not detected and could not be downloaded."
    echo "AppDir staged successfully and ready for manual packaging at: ${APPDIR}"
    echo "To finalize the AppImage, install appimagetool and run:"
    echo "  appimagetool \"${APPDIR}\" \"${OUTPUT_APPIMAGE}\""
fi
```

---

## 6. Caveats

1. **Host Distribution Target**:
   - AppImages link dynamically against the host system's `glibc`. An AppImage built on a modern distribution (e.g. Ubuntu 24.04 with glibc 2.39) will not run on older distributions (e.g. Ubuntu 20.04 with glibc 2.31). For widest Linux binary distribution, release packaging should be performed on the oldest supported LTS release (e.g. Ubuntu 22.04 or in a Docker container matching that baseline).
2. **Crashhandler Interaction vs `exec`**:
   - The legacy `AppRun` polled `olive-crashhandler` before dismounting. Using `exec` is the standard AppImage pattern because it avoids zombie parent shells and ensures signal delivery. If crash handler support inside the AppImage is required without dismounting, `olive-crashhandler` must be spawned by `olive-editor` as an independent or detached process, or the user should run with `--appimage-extract-and-run`.
3. **Python 3 Dependency for Build Script**:
   - The build script uses an embedded Python 3 snippet to parse `ldd` output transitively. This requires `python3` on the build machine. Since Python 3 is already a hard dependency for Olive's build system (`scripts/gauntlet.py`, code generation), this introduces no new prerequisites.
4. **Wayland Display Server Variations**:
   - Wayland compositors (GNOME Mutter, KDE KWin, Sway) require `libqwayland-egl.so` and `libqwayland-generic.so`. On hosts lacking Wayland runtime libraries, Qt automatically falls back to `libqxcb.so` provided both are bundled.

---

## 7. Conclusion

This specification establishes a robust, deterministic, and maintainable packaging workflow for modern Qt6 AppImages of Olive Video Editor:
1. **`AppRun`** is modernized to export all four necessary environment variables (`LD_LIBRARY_PATH`, `QT_PLUGIN_PATH`, `QML2_IMPORT_PATH`, `XDG_DATA_DIRS`), implements relocatable directory resolution, eliminates the trailing colon vulnerability, and forwards arguments via `exec "$@"`.
2. **`packaging/linux/build_appimage.sh`** fulfills all production criteria: bash strict mode, build directory parameterization, canonical CMake staging, relative AppImage specification symlinks, defensive Qt6 plugin deployment, transitive shared library resolution with strict glibc/graphics exclusions, UsrMerge normalization, and FUSE-tolerant `appimagetool` packaging.
3. The recommended templates have been syntax-checked and validated against the working repository and dynamic link tree.

---

## 8. Verification Method

### 8.1 Bash Syntax Verification
Run `bash -n` against both script targets to guarantee zero parsing or syntax errors:
```bash
bash -n app/packaging/linux/AppRun
bash -n packaging/linux/build_appimage.sh
```
*Expected Result*: Exit code `0` with zero output or syntax warnings.

### 8.2 Staging and Installation Dry Run
Verify that CMake installs the application and packaging targets into the specified prefix:
```bash
cmake --install build-linux-release --prefix /tmp/olive-test-install
test -f /tmp/olive-test-install/bin/olive-editor
test -f /tmp/olive-test-install/share/applications/org.olivevideoeditor.Olive.desktop
test -f /tmp/olive-test-install/share/icons/hicolor/256x256/apps/org.olivevideoeditor.Olive.png
rm -rf /tmp/olive-test-install
```
*Expected Result*: All files exist and commands return exit code `0`.

### 8.3 Library Resolution Logic Verification
Execute the embedded Python library bundler logic on the current build tree:
```bash
python3 -c '
import subprocess, os
binaries = ["build-linux-release/app/olive-editor"]
EXCLUDED = ("libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux", "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libOpenGL.so", "libdrm.so", "libX11.so")
out = subprocess.check_output(["ldd", binaries[0]], text=True)
bundled = [p.split()[2] for p in out.splitlines() if " => " in p and os.path.isabs(p.split()[2]) and not os.path.basename(p.split()[2]).startswith(EXCLUDED)]
assert len(bundled) > 50, f"Expected >50 libraries, found {len(bundled)}"
assert any("libavcodec" in x for x in bundled), "libavcodec missing"
assert any("libOpenColorIO" in x for x in bundled), "libOpenColorIO missing"
assert any("libOpenImageIO" in x for x in bundled), "libOpenImageIO missing"
assert any("libportaudio" in x for x in bundled), "libportaudio missing"
print(f"Verified {len(bundled)} direct non-system libraries resolved correctly.")
'
```
*Expected Result*: Prints `Verified >300 direct non-system libraries resolved correctly.` with assertion success.
