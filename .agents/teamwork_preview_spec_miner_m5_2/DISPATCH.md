## 2026-09-20T14:17:41Z

Mine precise requirements, runtime specifications, and deterministic bundling logic for modern Linux Qt6 AppImages:
1. AppRun environment specifications:
   - Full export of LD_LIBRARY_PATH (including x86_64-linux-gnu), QT_PLUGIN_PATH, QML2_IMPORT_PATH, XDG_DATA_DIRS.
   - Relocatable path determination via readlink/dirname.
   - Exec invocation passing through "$@".
2. `packaging/linux/build_appimage.sh` script requirements:
   - Bash strict mode (`set -euo pipefail`).
   - Command line argument support for build directory (e.g. `BUILD_DIR="${1:-build-linux-release}"`).
   - Staging installation into `AppDir/usr` via `cmake --install`.
   - Desktop and icon root symlinks per AppImage specification (`.desktop`, `png`, `.DirIcon`).
   - Copying Qt6 plugins: `platforms` (`libqxcb.so`, `libqwayland-*.so`), `imageformats`, `iconengines`, `platformthemes`.
   - Shared library bundling: resolving non-system runtime libraries (FFmpeg: `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`, `libavfilter`; OCIO: `libOpenColorIO`; OIIO: `libOpenImageIO`; OpenEXR/Imath; PortAudio) while avoiding bundling core glibc/graphics driver libs (`libc.so`, `libm.so`, `libpthread.so`, `libdl.so`, `libGL.so`, `libdrm.so`, `libX11.so`).
   - Packaging invocation using `appimagetool` with fallback/check.
   - Verification command compatibility (`bash -n packaging/linux/build_appimage.sh`).
