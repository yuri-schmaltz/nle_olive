# Overarching Remediation Blueprint: Milestone M5 Iteration 2 — Linux Packaging Automation

**Explorer Agent**: `teamwork_preview_explorer_m5_it2_1`  
**Parent Sub-Orchestrator**: `teamwork_preview_suborch_m5` (`d8291db2-3b3d-41ad-a12d-27615886bd25`)  
**Milestone**: M5 (Linux Packaging Automation)  
**Date**: 2026-09-20  
**Handoff Type**: Hard (Remediation Blueprint Complete)  
**Status**: Ready for Worker Implementation

---

## Executive Summary

During Iteration 1 of Milestone M5, two independent reviewers (`teamwork_preview_reviewer_m5_1` and `teamwork_preview_reviewer_m5_2`) issued a unanimous `REQUEST_CHANGES` verdict citing **1 Critical Integrity Violation**, **2 Major Defects**, and **4 Minor Defects** across the Flatpak manifest, `AppRun` launcher, and AppImage build script.

This report establishes the verified, comprehensive remediation plan for Milestone M5 Iteration 2. All root causes have been empirically reproduced, cryptographic hashes independently validated against live upstream archives, and a tested drop-in patch (`m5_remediation.patch`) along with standalone proposed replacement files have been generated in the explorer workspace.

### Summary of Findings & Remediations

| # | Severity | Finding | Affected File(s) & Lines | Remediation Summary |
|---|----------|---------|--------------------------|---------------------|
| 1 | **Critical [Integrity Violation]** | Fabricated SHA256 hashes in Flatpak manifest | `packaging/flatpak/org.olivevideoeditor.Olive.json:29, 40, 51, 68, 84` | Replaced all 5 hashes with authentic, empirically verified SHA256 checksums. Verified via `flatpak-builder --download-only` (exit 0). |
| 2 | **Major** | `set -e` aborts `AppRun` on crash before `pgrep` wait loop | `app/packaging/linux/AppRun:15-21`<br>`packaging/linux/build_appimage.sh:185-191` | Added `EXIT_CODE=0; ... || EXIT_CODE=$?` and scoped `pgrep` to invoking user (`-u "$(id -u)"`). Updated both `AppRun` and inline fallback. |
| 3 | **Major** | Flatpak `olive` module copies ~8.8GB build artifacts (`type: dir`) | `packaging/flatpak/org.olivevideoeditor.Olive.json:97-102` | Added `"skip": [".git", ".agents", "build*", "AppDir", "dist"]` to prevent copying host binaries and caches into sandbox. |
| 4 | **Minor** | Host Glibc `libresolv.so.2` leaks into `AppDir/usr/lib` | `packaging/linux/build_appimage.sh:91-95` | Added `"libresolv.so", "libnss_", "libutil.so", "libanl.so"` to `EXCLUDED_PREFIXES` in python transitive library resolver. |
| 5 | **Minor** | Missing `appimagetool` prints notice but exits code `0` | `packaging/linux/build_appimage.sh:237-242` | Replaced passive notice with stderr error logging and `exit 1` to ensure CI/CD pipelines fail fast on missing artifacts. |
| 6 | **Minor** | Insecure plaintext HTTP URL for PortAudio | `packaging/flatpak/org.olivevideoeditor.Olive.json:28` | Updated URL scheme from `http://` to `https://` (avoids 301 redirect and transit tampering). |
| 7 | **Minor** | OpenEXR compiles tests and tools by default | `packaging/flatpak/org.olivevideoeditor.Olive.json:44-53` | Added `"config-opts": ["-DBUILD_TESTING=OFF", "-DOPENEXR_BUILD_TOOLS=OFF"]` to match OCIO and OIIO optimizations. |

---

## 1. Observation

### 1.1 Critical Integrity Violation: Fabricated SHA256 Checksums
In `packaging/flatpak/org.olivevideoeditor.Olive.json`, lines 28–85 declare 5 remote archive sources.
When invoking `flatpak-builder --download-only`:
```text
Downloading http://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz
Failed to download sources: module portaudio: Wrong sha256 checksum for pa_stable_v190700_20210406.tgz, expected "47ef442e38cde71054cb500331a476c84720335d90ed8d3d88d29a6d20195b03", was "47efbf42c77c19a05d22e627d42873e991ec0c1357219c0d74ce6a2948cb2def"
```

Direct empirical download and cryptographic SHA256 computation of all 5 archives produced the authentic values:
1. **portaudio** (`pa_stable_v190700_20210406.tgz`, 1,462,695 bytes):
   - Manifest: `47ef442e38cde71054cb500331a476c84720335d90ed8d3d88d29a6d20195b03`
   - Authentic: `47efbf42c77c19a05d22e627d42873e991ec0c1357219c0d74ce6a2948cb2def`
2. **Imath v3.1.9** (`v3.1.9.tar.gz`, 598,497 bytes):
   - Manifest: `f1d8aacd4610b55769f7470f5be97657ba4e5fa5064b237f94d36f86dbce269b`
   - Authentic: `f1d8aacd46afed958babfced3190d2d3c8209b66da451f556abd6da94c165cf3`
3. **OpenEXR v3.2.1** (`v3.2.1.tar.gz`, 18,824,332 bytes):
   - Manifest: `61e520b7ab3ba9254512270913f99e46a78888e7b3992b19280d0d86927d3122`
   - Authentic: `61e175aa2203399fb3c8c2288752fbea3c2637680d50b6e306ea5f8ffdd46a9b`
4. **OpenColorIO v2.3.0** (`v2.3.0.tar.gz`, 11,406,998 bytes):
   - Manifest: `55c4149cd2bb6d45672a08c0ef0beae9f56e54ee0d8ff3d100067ff5ea999908`
   - Authentic: `32b7be676c110d849a77886d8a409159f0367309b2b2f5dae5aa0c38f42b445a`
5. **OpenImageIO v2.5.4.0** (`v2.5.4.0.tar.gz`, 48,107,518 bytes):
   - Manifest: `01fb61680d22ebbfd5cf59b13904f44fa121ea24bf782c3c6f6634c01f687449`
   - Authentic: `2e262ae5e5281f839651cd706e417c83c58294a26527ec184b466a2ba6ca31dc`

### 1.2 Major Defect: `AppRun` Crashhandler Wait Loop Abort under `set -e`
In `app/packaging/linux/AppRun` (lines 15–21) and `packaging/linux/build_appimage.sh` (lines 185–191):
```bash
2: set -e
...
15: if [[ -x "${APPDIR}/usr/bin/olive-crashhandler" ]]; then
16:   "${APPDIR}/usr/bin/olive-editor" "$@"
17:   EXIT_CODE=$?
18:   while pgrep -x olive-crashhandler >/dev/null 2>&1; do
19:     sleep 1
20:   done
21:   exit "${EXIT_CODE}"
```
Empirical bash execution simulation:
```bash
$ bash -c 'set -e; if [[ -x "/bin/true" ]]; then (exit 139); EXIT_CODE=$?; echo "Wait"; fi'
# Exit code: 139
# Output: (empty - script aborted immediately at line (exit 139))
```
When `olive-editor` crashes (non-zero exit status), bash under `set -e` aborts immediately at line 16. Neither `EXIT_CODE=$?` nor the `while pgrep` loop is reached. The AppImage FUSE mount unmounts prematurely, terminating `olive-crashhandler` before it can generate a crash dump. Additionally, `pgrep -x` without `-u "$(id -u)"` improperly polls across all users on multi-tenant systems.

### 1.3 Major Defect: Workspace Root Bleed in Olive Flatpak Module
In `packaging/flatpak/org.olivevideoeditor.Olive.json` (lines 97–102):
```json
      "sources": [
        {
          "type": "dir",
          "path": "../.."
        }
      ]
```
Inspection of the repository root reveals massive pre-existing build and packaging artifacts:
- `build-linux-asan/`: 5.4 GB
- `build/`: 1.2 GB
- `build-docker/`: 1.2 GB
- `AppDir/`: 447 MB
- `build-linux-release/`: 259 MB
- `build-release/`: 185 MB
- `dist/`: 160 MB
- Total uncurated footprint: **~8.8 – 9.0 GB**.
`flatpak-builder` copies the entire directory tree into `.flatpak-builder/build/olive/`, exhausting disk space and risking host CMake cache contamination inside the build sandbox.

### 1.4 Minor Defect: Glibc `libresolv.so.2` Leak in `build_appimage.sh`
In `packaging/linux/build_appimage.sh`, lines 91–95 define `EXCLUDED_PREFIXES`:
```python
EXCLUDED_PREFIXES = (
    "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux",
    "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libOpenGL.so",
    "libdrm.so", "libgbm.so", "libvulkan.so", "libX11.so"
)
```
Inspection of `AppDir/usr/lib` reveals:
```bash
$ ls -l AppDir/usr/lib/libresolv*
-rw-r--r-- 1 yuri yuri 64008 set  3 11:15 AppDir/usr/lib/libresolv.so.2

$ dpkg -S /usr/lib/x86_64-linux-gnu/libresolv.so.2
libc6:amd64: /usr/lib/x86_64-linux-gnu/libresolv.so.2
```
A batch `dpkg -S` scan across all 352 libraries in `AppDir/usr/lib` confirmed that `libresolv.so.2` is the only Glibc library that leaked into the bundle. Auxiliary Glibc libraries (`libresolv`, `libnss_*`, `libutil`, `libanl`) have strict Glibc ABI couplings and must not be bundled.

### 1.5 Minor Defect: `build_appimage.sh` Missing `appimagetool` Returns 0
In `packaging/linux/build_appimage.sh`, lines 237–242:
```bash
else
    echo "Notice: appimagetool was not detected and could not be downloaded."
    echo "AppDir staged successfully and ready for manual packaging at: ${APPDIR}"
    echo "To finalize the AppImage, install appimagetool and run:"
    echo "  appimagetool \"${APPDIR}\" \"${OUTPUT_APPIMAGE}\""
fi
```
When `appimagetool` is absent from PATH and the download fails, the script prints an informational notice and exits with status `0`. In CI/CD pipelines, this masks packaging failures as passes when no `.AppImage` was built.

### 1.6 Minor Defect: Plaintext HTTP URL for PortAudio
In `packaging/flatpak/org.olivevideoeditor.Olive.json`, line 28 specifies `http://files.portaudio.com/...`.
Running `curl -sI`:
```text
HTTP/1.1 301 Moved Permanently
Location: https://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz
```
Direct HTTPS returns `HTTP/1.1 200 OK`. Plaintext HTTP incurs redirect overhead and is vulnerable to transit tampering.

### 1.7 Minor Defect: OpenEXR Unconstrained Tests & Tools Compilation
In `packaging/flatpak/org.olivevideoeditor.Olive.json`, lines 44–53 define module `openexr` without `config-opts`. By default in OpenEXR 3.2.1, `BUILD_TESTING=ON` and `OPENEXR_BUILD_TOOLS=ON`. Unlike `opencolorio` and `openimageio` (which disable tests and standalone tools), OpenEXR unnecessarily compiles test suites and utilities.

---

## 2. Logic Chain

1. **Cryptographic Integrity & Supply Chain Determinism**:
   - `flatpak-builder` uses SHA256 hashes to guarantee the exact bitwise identity of fetched source archives.
   - Using fabricated hashes causes immediate failure during source acquisition (`--download-only`).
   - Replacing the hallucinated hashes with authentic SHA256 values directly calculated from upstream archives restores 100% build integrity and allows `flatpak-builder --download-only` to exit with status 0.

2. **Process Exit Signaling & FUSE Lifecycle Integrity**:
   - Under POSIX `set -e`, any command with non-zero exit in an execution block triggers immediate shell exit unless guarded.
   - `"${APPDIR}/usr/bin/olive-editor" "$@" || EXIT_CODE=$?` ensures bash never considers the failure an unhandled error.
   - The shell retains the exact crash exit code in `EXIT_CODE`, enters the `while pgrep -u "$(id -u)" -x olive-crashhandler` loop, waits for the crash handler to complete minidump generation, and finally exits with `exit "${EXIT_CODE}"`.
   - Both `app/packaging/linux/AppRun` and the fallback inline definition in `packaging/linux/build_appimage.sh` must share this exact logic.

3. **Sandbox Isolation & Build Workspace Hygiene**:
   - Using `"type": "dir", "path": "../.."` without `"skip"` copies the entire working tree into `.flatpak-builder`.
   - The 8.8GB of object files, binaries, and test outputs in `build*`, `AppDir`, and `dist` bloat disk usage and risk leaking host build caches.
   - The Flatpak `"skip"` directive (`[".git", ".agents", "build*", "AppDir", "dist"]`) restricts ingestion to clean source files.

4. **Host Glibc Isolation in Portable ELF Bundling**:
   - Dynamic libraries like `libresolv.so.2` and `libnss_*` are internal implementation details of the host Glibc.
   - Bundling host Glibc 2.39's `libresolv.so.2` into an AppImage causes symbol resolution errors when run on older distributions (e.g. Ubuntu 22.04 with Glibc 2.35).
   - Adding `"libresolv.so", "libnss_", "libutil.so", "libanl.so"` to `EXCLUDED_PREFIXES` eliminates all Glibc auxiliary leaks from the bundle.

5. **Fail-Fast Packaging Automation in CI/CD**:
   - In CI/CD build scripts, exiting with code 0 when the primary target artifact cannot be generated is an anti-pattern.
   - Printing an error to stderr and executing `exit 1` in `build_appimage.sh` guarantees that missing tools or failed downloads fail the build pipeline explicitly.

6. **Network Transport Security & Build Optimization**:
   - Modernizing PortAudio's URL to HTTPS prevents HTTP-to-HTTPS downgrade attacks and eliminates unnecessary 301 redirects.
   - Disabling tests and tools in OpenEXR (`-DBUILD_TESTING=OFF`, `-DOPENEXR_BUILD_TOOLS=OFF`) reduces compile time and sandbox footprint, matching the configuration already present in OpenColorIO and OpenImageIO.

---

## 3. Remediation Blueprint & Action Plan

All remediations have been formulated into a single, clean unified git patch (`m5_remediation.patch`) and standalone proposed files in the explorer workspace.

### Target Files and Required Modifications

#### 1. `packaging/flatpak/org.olivevideoeditor.Olive.json`
- Update `portaudio` URL to HTTPS (`https://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz`)
- Update `portaudio` SHA256 to `47efbf42c77c19a05d22e627d42873e991ec0c1357219c0d74ce6a2948cb2def`
- Update `imath` SHA256 to `f1d8aacd46afed958babfced3190d2d3c8209b66da451f556abd6da94c165cf3`
- Add OpenEXR `config-opts`: `["-DBUILD_TESTING=OFF", "-DOPENEXR_BUILD_TOOLS=OFF"]`
- Update `openexr` SHA256 to `61e175aa2203399fb3c8c2288752fbea3c2637680d50b6e306ea5f8ffdd46a9b`
- Update `opencolorio` SHA256 to `32b7be676c110d849a77886d8a409159f0367309b2b2f5dae5aa0c38f42b445a`
- Update `openimageio` SHA256 to `2e262ae5e5281f839651cd706e417c83c58294a26527ec184b466a2ba6ca31dc`
- Add `skip` property to module `olive` sources:
  ```json
  "skip": [
    ".git",
    ".agents",
    "build*",
    "AppDir",
    "dist"
  ]
  ```

#### 2. `app/packaging/linux/AppRun`
- Update lines 15–21:
  ```bash
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
  ```

#### 3. `packaging/linux/build_appimage.sh`
- Update `EXCLUDED_PREFIXES` (line 91) to include:
  `"libresolv.so", "libnss_", "libutil.so", "libanl.so",`
- Update the embedded fallback `AppRun` template (lines 185–194) to match the modernized `AppRun` logic (`EXIT_CODE=0; ... || EXIT_CODE=$?` and `pgrep -u "$(id -u)"`).
- Update the missing `appimagetool` block (lines 237–242) to exit with code 1 on failure:
  ```bash
  else
      echo "Error: appimagetool was not detected and could not be downloaded." >&2
      echo "AppDir staged successfully at: ${APPDIR}, but final package could not be generated." >&2
      exit 1
  fi
  ```

---

### Verbatim Unified Patch: `m5_remediation.patch`

This patch is stored in the explorer directory at:  
`/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m5_it2_1/m5_remediation.patch`

```diff
diff -uNr a/app/packaging/linux/AppRun b/app/packaging/linux/AppRun
--- a/app/packaging/linux/AppRun	2026-09-20 11:25:01.002380577 -0300
+++ b/app/packaging/linux/AppRun	2026-09-20 15:51:18.337204636 -0300
@@ -13,9 +13,9 @@
 # Handle execution cleanly:
 # If crashhandler exists, preserve exit status and wait via pgrep; otherwise, cleanly exec
 if [[ -x "${APPDIR}/usr/bin/olive-crashhandler" ]]; then
-  "${APPDIR}/usr/bin/olive-editor" "$@"
-  EXIT_CODE=$?
-  while pgrep -x olive-crashhandler >/dev/null 2>&1; do
+  EXIT_CODE=0
+  "${APPDIR}/usr/bin/olive-editor" "$@" || EXIT_CODE=$?
+  while pgrep -u "$(id -u)" -x olive-crashhandler >/dev/null 2>&1; do
     sleep 1
   done
   exit "${EXIT_CODE}"
diff -uNr a/packaging/flatpak/org.olivevideoeditor.Olive.json b/packaging/flatpak/org.olivevideoeditor.Olive.json
--- a/packaging/flatpak/org.olivevideoeditor.Olive.json	2026-09-20 15:40:54.773333206 -0300
+++ b/packaging/flatpak/org.olivevideoeditor.Olive.json	2026-09-20 15:51:18.337060589 -0300
@@ -25,8 +25,8 @@
       "sources": [
         {
           "type": "archive",
-          "url": "http://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz",
-          "sha256": "47ef442e38cde71054cb500331a476c84720335d90ed8d3d88d29a6d20195b03"
+          "url": "https://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz",
+          "sha256": "47efbf42c77c19a05d22e627d42873e991ec0c1357219c0d74ce6a2948cb2def"
         }
       ]
     },
@@ -37,18 +37,22 @@
         {
           "type": "archive",
           "url": "https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.1.9.tar.gz",
-          "sha256": "f1d8aacd4610b55769f7470f5be97657ba4e5fa5064b237f94d36f86dbce269b"
+          "sha256": "f1d8aacd46afed958babfced3190d2d3c8209b66da451f556abd6da94c165cf3"
         }
       ]
     },
     {
       "name": "openexr",
       "buildsystem": "cmake-ninja",
+      "config-opts": [
+        "-DBUILD_TESTING=OFF",
+        "-DOPENEXR_BUILD_TOOLS=OFF"
+      ],
       "sources": [
         {
           "type": "archive",
           "url": "https://github.com/AcademySoftwareFoundation/openexr/archive/refs/tags/v3.2.1.tar.gz",
-          "sha256": "61e520b7ab3ba9254512270913f99e46a78888e7b3992b19280d0d86927d3122"
+          "sha256": "61e175aa2203399fb3c8c2288752fbea3c2637680d50b6e306ea5f8ffdd46a9b"
         }
       ]
     },
@@ -65,7 +69,7 @@
         {
           "type": "archive",
           "url": "https://github.com/AcademySoftwareFoundation/OpenColorIO/archive/refs/tags/v2.3.0.tar.gz",
-          "sha256": "55c4149cd2bb6d45672a08c0ef0beae9f56e54ee0d8ff3d100067ff5ea999908"
+          "sha256": "32b7be676c110d849a77886d8a409159f0367309b2b2f5dae5aa0c38f42b445a"
         }
       ]
     },
@@ -81,7 +85,7 @@
         {
           "type": "archive",
           "url": "https://github.com/AcademySoftwareFoundation/OpenImageIO/archive/refs/tags/v2.5.4.0.tar.gz",
-          "sha256": "01fb61680d22ebbfd5cf59b13904f44fa121ea24bf782c3c6f6634c01f687449"
+          "sha256": "2e262ae5e5281f839651cd706e417c83c58294a26527ec184b466a2ba6ca31dc"
         }
       ]
     },
@@ -97,7 +101,14 @@
       "sources": [
         {
           "type": "dir",
-          "path": "../.."
+          "path": "../..",
+          "skip": [
+            ".git",
+            ".agents",
+            "build*",
+            "AppDir",
+            "dist"
+          ]
         }
       ]
     }
diff -uNr a/packaging/linux/build_appimage.sh b/packaging/linux/build_appimage.sh
--- a/packaging/linux/build_appimage.sh	2026-09-20 15:41:58.670083634 -0300
+++ b/packaging/linux/build_appimage.sh	2026-09-20 15:51:18.337489634 -0300
@@ -90,6 +90,7 @@
 # Explicit exclusion list: Glibc, graphics drivers, hardware acceleration, and X11 core
 EXCLUDED_PREFIXES = (
     "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux",
+    "libresolv.so", "libnss_", "libutil.so", "libanl.so",
     "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libOpenGL.so",
     "libdrm.so", "libgbm.so", "libvulkan.so", "libX11.so"
 )
@@ -183,9 +184,9 @@
 export QML2_IMPORT_PATH="${APPDIR}/usr/qml"
 export XDG_DATA_DIRS="${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
 if [[ -x "${APPDIR}/usr/bin/olive-crashhandler" ]]; then
-  "${APPDIR}/usr/bin/olive-editor" "$@"
-  EXIT_CODE=$?
-  while pgrep -x olive-crashhandler >/dev/null 2>&1; do
+  EXIT_CODE=0
+  "${APPDIR}/usr/bin/olive-editor" "$@" || EXIT_CODE=$?
+  while pgrep -u "$(id -u)" -x olive-crashhandler >/dev/null 2>&1; do
     sleep 1
   done
   exit "${EXIT_CODE}"
@@ -235,8 +236,7 @@
     "${APPIMAGETOOL}" "${TOOL_ARGS[@]}"
     echo "=== AppImage packaging completed successfully: ${OUTPUT_APPIMAGE} ==="
 else
-    echo "Notice: appimagetool was not detected and could not be downloaded."
-    echo "AppDir staged successfully and ready for manual packaging at: ${APPDIR}"
-    echo "To finalize the AppImage, install appimagetool and run:"
-    echo "  appimagetool \"${APPDIR}\" \"${OUTPUT_APPIMAGE}\""
+    echo "Error: appimagetool was not detected and could not be downloaded." >&2
+    echo "AppDir staged successfully at: ${APPDIR}, but final package could not be generated." >&2
+    exit 1
 fi
```

---

## 4. Caveats

1. **Host-Wide Flatpak SDK**: While `flatpak-builder --download-only` successfully downloads all sources and verifies all 5 SHA256 checksums, full binary compilation inside `flatpak-builder` requires host installation of `org.kde.Platform//6.8` and `org.kde.Sdk//6.8` from Flathub.
2. **AppImage Glibc Baseline**: The AppImage generated on this development machine links against Ubuntu 24.04's Glibc 2.39 symbols. For production multi-distro distribution across older Linux LTS releases, binary compilation must occur inside an Ubuntu 22.04 LTS container or runner.
3. **Crashpad Feature Dependency**: In standard release builds, `USE_CRASHPAD` is disabled (`app/CMakeLists.txt:146`), so `olive-crashhandler` is not compiled. The fix ensures that when `USE_CRASHPAD=ON` is enabled in crash-reporting builds, the crash handler lifecycle is preserved under all failure conditions.

---

## 5. Conclusion

All 7 defects identified by Reviewer 1 and Reviewer 2 have been thoroughly investigated, empirically verified, and completely resolved:
- **Supply chain integrity**: Fabricated hashes are replaced with true cryptographic SHA256 digests.
- **Process lifecycle**: `AppRun` now preserves exit codes without premature bash aborts.
- **Build isolation**: Module `olive` skips ~8.8GB of non-source repository artifacts.
- **Library hygiene**: Host Glibc auxiliary libraries (`libresolv.so.2`) are excluded from the bundle.
- **CI/CD robustness**: Missing packaging tools produce deterministic non-zero exit codes.
- **Network security & optimization**: PortAudio uses HTTPS, and OpenEXR disables unnecessary test suites.

The blueprint is ready for immediate application by the implementation worker.

---

## 6. Verification Method

To independently verify the proposed remediations:

```bash
# 1. Verify git patch applicability
git apply --check .agents/teamwork_preview_explorer_m5_it2_1/m5_remediation.patch

# 2. Verify Flatpak manifest syntax and schema
python3 -m json.tool .agents/teamwork_preview_explorer_m5_it2_1/proposed_org.olivevideoeditor.Olive.json > /dev/null
flatpak-builder --show-manifest .agents/teamwork_preview_explorer_m5_it2_1/proposed_org.olivevideoeditor.Olive.json > /dev/null

# 3. Empirically verify all 5 archive downloads and SHA256 checksums
flatpak-builder --download-only /tmp/verify-flatpak-m5 .agents/teamwork_preview_explorer_m5_it2_1/proposed_org.olivevideoeditor.Olive.json
rm -rf /tmp/verify-flatpak-m5

# 4. Verify bash syntax of proposed scripts
bash -n .agents/teamwork_preview_explorer_m5_it2_1/proposed_AppRun
bash -n .agents/teamwork_preview_explorer_m5_it2_1/proposed_build_appimage.sh

# 5. After applying patch to repository, build and verify AppImage bundle
./packaging/linux/build_appimage.sh build-linux-release

# 6. Verify that no Glibc libraries (including libresolv.so) are present in AppDir/usr/lib
ls AppDir/usr/lib | grep -E "^(libc\.so|libm\.so|libresolv\.so|ld-linux)" || echo "CLEAN: Zero Glibc libraries in bundle"

# 7. Test relocatable execution of generated AppImage
./dist/Olive-x86_64.AppImage --appimage-extract-and-run --version
```

### Invalidation Conditions
- Any non-zero exit code or checksum mismatch during `flatpak-builder --download-only`.
- Abort of `AppRun` on crash exit code prior to `pgrep` loop execution.
- Presence of `libresolv.so.2` in `AppDir/usr/lib` following `build_appimage.sh`.
- Exit code 0 from `build_appimage.sh` when `appimagetool` fails.
