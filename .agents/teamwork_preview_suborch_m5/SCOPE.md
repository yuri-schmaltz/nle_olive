# Scope: Milestone M5 (Linux Packaging Automation)

## Objectives
Implement reproducible Linux packaging for Olive Video Editor via AppImage script and Flatpak manifest.

## Requirements & Scope Boundaries
1. **AppImage Packaging**:
   - Modernize `app/packaging/linux/AppRun` to export complete runtime environment:
     - `LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"`
     - `QT_PLUGIN_PATH="$APPDIR/usr/plugins"`
     - `QML2_IMPORT_PATH="$APPDIR/usr/qml"`
     - `XDG_DATA_DIRS="$APPDIR/usr/share:$XDG_DATA_DIRS"`
   - Create reproducible build/packaging script `packaging/linux/build_appimage.sh`:
     - Packages Olive binary, FFmpeg shared libs, OpenColorIO, OpenImageIO, and Qt6 libraries + platform plugins (`platforms/libqxcb.so`, `wayland`).
     - Supports desktop integration, MIME types, and icons.
     - Includes integrity and sanity checks.
2. **Flatpak Manifest**:
   - Create complete Flatpak manifest `packaging/flatpak/org.olivevideoeditor.Olive.json` targeting KDE Application Runtime 6.8+ (`org.kde.Platform` / `org.kde.Sdk`).
   - Define modules for all third-party dependencies not in KDE runtime (Imath, OpenEXR, OpenColorIO, OpenImageIO, PortAudio) and Olive Editor itself.
3. **Verification**:
   - Verify script execution syntax (`bash -n packaging/linux/build_appimage.sh`).
   - Validate JSON manifest structure (`python3 -m json.tool packaging/flatpak/org.olivevideoeditor.Olive.json`).
