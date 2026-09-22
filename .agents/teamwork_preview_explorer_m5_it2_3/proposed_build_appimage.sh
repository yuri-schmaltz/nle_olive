#!/usr/bin/env bash
# ==============================================================================
# Olive Video Editor - Modern Qt6 Linux AppImage Bundling Script
# ==============================================================================
set -euo pipefail

# 1. Resolve repository root and build paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

STRICT="${STRICT_PACKAGING:-${STRICT:-1}}"
BUILD_DIR=""

for arg in "$@"; do
    case "${arg}" in
        --strict)
            STRICT=1
            ;;
        --no-strict)
            STRICT=0
            ;;
        -h|--help)
            echo "Usage: $0 [options] [build-directory]"
            echo ""
            echo "Options:"
            echo "  --strict      Fail with exit code 1 if appimagetool is missing (default)"
            echo "  --no-strict   Do not fail if appimagetool is missing; stage AppDir only"
            echo "  -h, --help    Show this help message"
            exit 0
            ;;
        *)
            if [ -z "${BUILD_DIR}" ]; then
                BUILD_DIR="${arg}"
            fi
            ;;
    esac
done

BUILD_DIR="${BUILD_DIR:-build-linux-release}"
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

for plugin_group in platforms imageformats iconengines platformthemes xcbglintegrations; do
    if [ -d "${QT_PLUGIN_DIR}/${plugin_group}" ]; then
        cp -r "${QT_PLUGIN_DIR}/${plugin_group}" "${APPDIR}/usr/plugins/"
    fi
done

for wayland_dir in "${QT_PLUGIN_DIR}"/wayland-*; do
    if [ -d "${wayland_dir}" ]; then
        cp -r "${wayland_dir}" "${APPDIR}/usr/plugins/"
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

# Explicit exclusion list: Glibc core/auxiliary, graphics drivers, hardware acceleration, and X11 core
EXCLUDED_PREFIXES = (
    "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux",
    "libresolv.so", "libnss_", "libutil.so", "libanl.so",
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
                # Crucial UsrMerge handling: accept paths starting with /lib, /usr/lib, /lib64, /usr/lib64, /usr/local/lib
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
if [ -f "${REPO_ROOT}/app/packaging/linux/AppRun" ]; then
    cp "${REPO_ROOT}/app/packaging/linux/AppRun" "${APPDIR}/AppRun"
else
    cat << 'RUNEOF' > "${APPDIR}/AppRun"
#!/usr/bin/env bash
set -e
APPDIR="${APPDIR:-$(dirname "$(readlink -f "${0}")")}"
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${APPDIR}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${APPDIR}/usr/plugins"
export QML2_IMPORT_PATH="${APPDIR}/usr/qml"
export XDG_DATA_DIRS="${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
if [[ -x "${APPDIR}/usr/bin/olive-crashhandler" ]]; then
  EXIT_CODE=0
  "${APPDIR}/usr/bin/olive-editor" "$@" || EXIT_CODE=$?
  while pgrep -u "$(id -u)" -x olive-crashhandler >/dev/null 2>&1; do
    sleep 1
  done
  exit "${EXIT_CODE}"
else
  exec "${APPDIR}/usr/bin/olive-editor" "$@"
fi
RUNEOF
fi
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
    TOOL_ARGS=()
    # Container / FUSE check
    if [ "${APPIMAGE_EXTRACT_AND_RUN:-0}" = "1" ] || [ ! -c /dev/fuse ]; then
        export APPIMAGE_EXTRACT_AND_RUN=1
        TOOL_ARGS+=("--appimage-extract-and-run")
    fi
    # Use -n (--no-appstream) to prevent packaging abort from upstream metainfo validation warnings
    TOOL_ARGS+=("-n")
    TOOL_ARGS+=("${APPDIR}" "${OUTPUT_APPIMAGE}")
    if ! "${APPIMAGETOOL}" "${TOOL_ARGS[@]}"; then
        echo "Error: appimagetool failed to generate package at: ${OUTPUT_APPIMAGE}" >&2
        exit 1
    fi
    echo "=== AppImage packaging completed successfully: ${OUTPUT_APPIMAGE} ==="
else
    echo "Error: appimagetool was not detected and could not be downloaded." >&2
    echo "AppDir staged successfully at: ${APPDIR}, but final package could not be generated." >&2
    echo "To finalize the AppImage, install appimagetool and run:" >&2
    echo "  appimagetool \"${APPDIR}\" \"${OUTPUT_APPIMAGE}\"" >&2
    if [ "${STRICT}" = "1" ] || [ -n "${CI:-}" ] || [ -n "${GITHUB_ACTIONS:-}" ]; then
        exit 1
    fi
    echo "Notice: Exiting with status 0 as non-strict mode was requested."
fi
