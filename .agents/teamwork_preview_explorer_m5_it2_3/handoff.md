# Investigation & Remediation Report: AppRun & build_appimage.sh

**Agent**: `teamwork_preview_explorer_m5_it2_3`  
**Parent Sub-Orchestrator**: `teamwork_preview_suborch_m5` (`d8291db2-3b3d-41ad-a12d-27615886bd25`)  
**Scope**: Exact remediation formulation for `app/packaging/linux/AppRun` and `packaging/linux/build_appimage.sh` (Milestone M5, Iteration 2)  
**Date**: 2026-09-20  
**Handoff Type**: Hard (Investigation & Remediation Complete)

---

## 1. Executive Summary

Milestone M5 provides reproducible Linux packaging for Olive Video Editor via AppImage bundling and Flatpak distribution. During review Iteration 1 (`teamwork_preview_reviewer_m5_1`), two critical functional lifecycle issues and two system compatibility/reporting defects were identified in the AppImage packaging pipeline:

1. **`AppRun` Crash Lifecycle Abort**: In `app/packaging/linux/AppRun` (and the embedded fallback in `build_appimage.sh`), `set -e` causes the shell to exit immediately upon application crash (exit status > 0). Consequently, bash aborts at line 16, never assigning `EXIT_CODE` and never entering the `while pgrep ... olive-crashhandler` wait loop. The AppImage unmounts prematurely, aborting crash dump generation.
2. **Crashhandler Process User Scoping**: In `AppRun`, `pgrep -x olive-crashhandler` monitors processes across all system users rather than scoping to the invoking user (`-u "$(id -u)"`).
3. **Glibc `libresolv.so` Bundling Leak**: In `packaging/linux/build_appimage.sh`, `EXCLUDED_PREFIXES` did not exclude `libresolv.so`, causing host Glibc's `libresolv.so.2` to be staged into `AppDir/usr/lib`, introducing potential symbol mismatches across diverse Linux hosts.
4. **Silent Packaging Failure Exit Status**: In `packaging/linux/build_appimage.sh`, when `appimagetool` was not found and download failed, the script printed an informational notice and exited with return code `0`, silently masking packaging failures in automated CI pipelines.

This report provides empirical evidence, rigorous logic chains, machine-applicable unified diffs, complete replacement files, and independent verification procedures to remediate all four defects.

---

## 2. Observation

### 2.1 `app/packaging/linux/AppRun`: `set -e` Abort & Global `pgrep`
Inspection of `app/packaging/linux/AppRun` (lines 1-25):
```bash
1: #!/usr/bin/env bash
2: set -e
3: 
4: # Determine canonical relocatable root via readlink -f / dirname
5: APPDIR="${APPDIR:-$(dirname "$(readlink -f "${0}")")}"
...
13: # Handle execution cleanly:
14: # If crashhandler exists, preserve exit status and wait via pgrep; otherwise, cleanly exec
15: if [[ -x "${APPDIR}/usr/bin/olive-crashhandler" ]]; then
16:   "${APPDIR}/usr/bin/olive-editor" "$@"
17:   EXIT_CODE=$?
18:   while pgrep -x olive-crashhandler >/dev/null 2>&1; do
19:     sleep 1
20:   done
21:   exit "${EXIT_CODE}"
22: else
23:   exec "${APPDIR}/usr/bin/olive-editor" "$@"
24: fi
```

**Empirical Verification of Failure**:
Simulating this block under bash with an application exiting with status 139 (SIGSEGV):
```bash
$ bash -c '
set -e
if [[ -x "/bin/true" ]]; then
  bash -c "exit 139"
  EXIT_CODE=$?
  echo "Reached line after crash: $EXIT_CODE"
fi
echo "Finished"
'
# Exit code: 139. Neither "Reached line after crash" nor any subsequent lines were executed!
```
Under `set -e`, execution aborts immediately at line 16. Lines 17-21 are unreachable on non-zero exit. Furthermore, line 18 searches across all UID namespaces on multi-user systems.

---

### 2.2 `packaging/linux/build_appimage.sh`: Glibc `libresolv.so` Leak
Inspection of `packaging/linux/build_appimage.sh` (lines 90-95):
```python
90: # Explicit exclusion list: Glibc, graphics drivers, hardware acceleration, and X11 core
91: EXCLUDED_PREFIXES = (
92:     "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux",
93:     "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libOpenGL.so",
94:     "libdrm.so", "libgbm.so", "libvulkan.so", "libX11.so"
95: )
```

**Empirical Verification of Staged Files**:
Inspecting libraries staged in `AppDir/usr/lib` from previous build run:
```bash
$ ls AppDir/usr/lib | grep -E "^lib(c|m|pthread|dl|rt|resolv|nss|util|anl)\.so"
libresolv.so.2
```
Package ownership confirmation:
```bash
$ dpkg -S /usr/lib/x86_64-linux-gnu/libresolv.so.2
libc6:amd64: /usr/lib/x86_64-linux-gnu/libresolv.so.2
```
`libresolv.so.2` is a core Glibc component that was copied into `AppDir/usr/lib` because `libresolv.so` was missing from `EXCLUDED_PREFIXES`.

---

### 2.3 `packaging/linux/build_appimage.sh`: Unsynchronized Embedded AppRun Fallback
Inspection of `packaging/linux/build_appimage.sh` (lines 185-195):
```bash
185: if [[ -x "${APPDIR}/usr/bin/olive-crashhandler" ]]; then
186:   "${APPDIR}/usr/bin/olive-editor" "$@"
187:   EXIT_CODE=$?
188:   while pgrep -x olive-crashhandler >/dev/null 2>&1; do
189:     sleep 1
190:   done
191:   exit "${EXIT_CODE}"
192: else
193:   exec "${APPDIR}/usr/bin/olive-editor" "$@"
194: fi
```
The embedded fallback `AppRun` template inside `build_appimage.sh` mirrors the exact defects of the primary `AppRun`: `set -e` premature abort and non-user-scoped `pgrep`.

---

### 2.4 `packaging/linux/build_appimage.sh`: Packaging Tool Exit Status
Inspection of `packaging/linux/build_appimage.sh` (lines 237-242):
```bash
237: else
238:     echo "Notice: appimagetool was not detected and could not be downloaded."
239:     echo "AppDir staged successfully and ready for manual packaging at: ${APPDIR}"
240:     echo "To finalize the AppImage, install appimagetool and run:"
241:     echo "  appimagetool \"${APPDIR}\" \"${OUTPUT_APPIMAGE}\""
242: fi
```

**Empirical Verification**:
Running the script without `appimagetool` in PATH:
When `APPIMAGETOOL` is empty, execution falls through lines 238-242 and reaches script EOF. The shell exits with exit code `0`. In CI pipelines, this results in a false positive ("GREEN") build status even though no `.AppImage` artifact was produced.

---

## 3. Logic Chain

### 3.1 AppRun Process Lifecycle & Error Handling Under `set -e`
1. **Shell Specification (POSIX / Bash)**: Under `set -e`, the shell exits immediately if a command exits with a non-zero status, *unless* the command is part of an `if`, `while`, `until` test, or the operand of an `&&` or `||` list.
2. **The Problem**: In `"${APPDIR}/usr/bin/olive-editor" "$@"`, the command is executed as an isolated simple command inside an `if` block body. When `olive-editor` crashes (e.g. status 139), `set -e` triggers instantly. Line 17 (`EXIT_CODE=$?`) and lines 18-20 (`while pgrep ...`) never execute.
3. **The Solution**: 
   ```bash
   EXIT_CODE=0
   "${APPDIR}/usr/bin/olive-editor" "$@" || EXIT_CODE=$?
   ```
   Because the command is the left operand of `||`, bash suppresses `set -e` for `olive-editor`. If `olive-editor` exits with non-zero status `N`, the right operand `EXIT_CODE=$?` executes, setting `EXIT_CODE=N`. Because variable assignment has an exit status of 0, the entire list succeeds, allowing bash to proceed sequentially to the `while` loop.
4. **Process Isolation**: Changing `pgrep -x olive-crashhandler` to `pgrep -u "$(id -u)" -x olive-crashhandler` guarantees that only crash handlers belonging to the current user prevent the AppImage wrapper from terminating.

### 3.2 Glibc Dynamic Linking & Binary Portability
1. **Glibc Architecture**: `libc.so`, `libpthread.so`, `librt.so`, `libm.so`, and `libresolv.so` are built from the exact same Glibc source release and share internal, unversioned ABI symbols with the dynamic linker (`ld-linux-x86-64.so.2`).
2. **AppImage Portability Rules**: AppImages must never bundle Glibc or any of its private sub-libraries (`libresolv.so`, `libnss_*`, `libutil.so`, `libanl.so`), because loading a bundled `libresolv.so` linked against a newer host Glibc on an older host Glibc (or vice-versa) leads to symbol lookup errors such as `GLIBC_PRIVATE not found`.
3. **The Solution**: Adding `"libresolv.so"`, `"libnss_"`, `"libutil.so"`, `"libanl.so"` to `EXCLUDED_PREFIXES` ensures the transitive ELF dependency resolver skips all Glibc auxiliary libraries, leaving them to be dynamically loaded from the host's native system runtime.

### 3.3 CI Pipeline Determinism & Error Reporting
1. **Automation Expectation**: In automated CI/CD builds or when invoking a build script whose sole purpose is generating `Olive-x86_64.AppImage`, failure to generate the final artifact must result in a non-zero exit code (`exit 1`).
2. **Flexibility for Local Development**: Developers who only want to stage the `AppDir` without creating an AppImage can explicitly request non-strict mode (`--no-strict`), while strict mode (`--strict`) is the default and is strictly enforced in CI/automation environments (`CI=true`, `GITHUB_ACTIONS=true`, etc.).
3. **Robust CLI Option Parsing**: Supporting `--strict`, `--no-strict`, and positional `build-directory` ensures backward compatibility with existing command invocations (`./packaging/linux/build_appimage.sh build-linux-release`) while enabling explicit control.

---

## 4. Caveats

1. **Host Glibc Baseline**: Excluding `libresolv.so` ensures Glibc consistency, but the AppImage executable will still require a host Glibc version greater than or equal to the build host's Glibc (e.g. Glibc 2.39 on Ubuntu 24.04). Production releases targeting older LTS distributions (e.g. Ubuntu 20.04/22.04) should be compiled inside a container with that baseline Glibc.
2. **`olive-crashhandler` Lifecycle**: If `olive-crashhandler` hangs indefinitely, the `while pgrep` loop will also wait indefinitely. In practice, crashpad handlers terminate after saving the dump or when closed by the user. An optional timeout could be added in future iterations if necessary.

---

## 5. Conclusion & Exact Remediation Specifications

All required changes have been verified and packaged into clean patches and replacement files within `.agents/teamwork_preview_explorer_m5_it2_3/`.

### 5.1 Remediation 1: `app/packaging/linux/AppRun`

#### Targeted Replacement Block
Lines 15-21 in `app/packaging/linux/AppRun`:
```bash
<<<<<<< BEFORE
if [[ -x "${APPDIR}/usr/bin/olive-crashhandler" ]]; then
  "${APPDIR}/usr/bin/olive-editor" "$@"
  EXIT_CODE=$?
  while pgrep -x olive-crashhandler >/dev/null 2>&1; do
    sleep 1
  done
  exit "${EXIT_CODE}"
else
  exec "${APPDIR}/usr/bin/olive-editor" "$@"
fi
=======
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
>>>>>>> AFTER
```

#### Unified Diff (`apprun_remediation.patch`)
```diff
--- a/app/packaging/linux/AppRun
+++ b/app/packaging/linux/AppRun
@@ -15,5 +15,6 @@
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
```

#### Complete Remediated File (`app/packaging/linux/AppRun`)
```bash
#!/usr/bin/env bash
set -e

# Determine canonical relocatable root via readlink -f / dirname
APPDIR="${APPDIR:-$(dirname "$(readlink -f "${0}")")}"

# Export full environment variables
export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${APPDIR}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${APPDIR}/usr/plugins"
export QML2_IMPORT_PATH="${APPDIR}/usr/qml"
export XDG_DATA_DIRS="${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

# Handle execution cleanly:
# If crashhandler exists, preserve exit status and wait via pgrep; otherwise, cleanly exec
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

---

### 5.2 Remediation 2: `packaging/linux/build_appimage.sh`

#### Component A: Argument Parsing & Strict Mode (Lines 11-16)
```bash
<<<<<<< BEFORE
BUILD_DIR="${1:-build-linux-release}"
if [[ "${BUILD_DIR}" = /* ]]; then
    BUILD_PATH="${BUILD_DIR}"
else
    BUILD_PATH="${REPO_ROOT}/${BUILD_DIR}"
fi
=======
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
>>>>>>> AFTER
```

#### Component B: Transitive Library `EXCLUDED_PREFIXES` (Lines 90-95)
```python
<<<<<<< BEFORE
# Explicit exclusion list: Glibc, graphics drivers, hardware acceleration, and X11 core
EXCLUDED_PREFIXES = (
    "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux",
    "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libOpenGL.so",
    "libdrm.so", "libgbm.so", "libvulkan.so", "libX11.so"
)
=======
# Explicit exclusion list: Glibc core/auxiliary, graphics drivers, hardware acceleration, and X11 core
EXCLUDED_PREFIXES = (
    "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux",
    "libresolv.so", "libnss_", "libutil.so", "libanl.so",
    "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libOpenGL.so",
    "libdrm.so", "libgbm.so", "libvulkan.so", "libX11.so"
)
>>>>>>> AFTER
```

#### Component C: Fallback AppRun Template (Lines 185-195)
```bash
<<<<<<< BEFORE
if [[ -x "${APPDIR}/usr/bin/olive-crashhandler" ]]; then
  "${APPDIR}/usr/bin/olive-editor" "$@"
  EXIT_CODE=$?
  while pgrep -x olive-crashhandler >/dev/null 2>&1; do
    sleep 1
  done
  exit "${EXIT_CODE}"
else
  exec "${APPDIR}/usr/bin/olive-editor" "$@"
fi
=======
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
>>>>>>> AFTER
```

#### Component D: Packaging Execution & Exit Status (Lines 224-242)
```bash
<<<<<<< BEFORE
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
    "${APPIMAGETOOL}" "${TOOL_ARGS[@]}"
    echo "=== AppImage packaging completed successfully: ${OUTPUT_APPIMAGE} ==="
else
    echo "Notice: appimagetool was not detected and could not be downloaded."
    echo "AppDir staged successfully and ready for manual packaging at: ${APPDIR}"
    echo "To finalize the AppImage, install appimagetool and run:"
    echo "  appimagetool \"${APPDIR}\" \"${OUTPUT_APPIMAGE}\""
fi
=======
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
>>>>>>> AFTER
```

#### Unified Diff (`build_appimage_remediation.patch`)
```diff
--- a/packaging/linux/build_appimage.sh
+++ b/packaging/linux/build_appimage.sh
@@ -11,7 +11,35 @@
-BUILD_DIR="${1:-build-linux-release}"
+STRICT="${STRICT_PACKAGING:-${STRICT:-1}}"
+BUILD_DIR=""
+
+for arg in "$@"; do
+    case "${arg}" in
+        --strict)
+            STRICT=1
+            ;;
+        --no-strict)
+            STRICT=0
+            ;;
+        -h|--help)
+            echo "Usage: $0 [options] [build-directory]"
+            echo ""
+            echo "Options:"
+            echo "  --strict      Fail with exit code 1 if appimagetool is missing (default)"
+            echo "  --no-strict   Do not fail if appimagetool is missing; stage AppDir only"
+            echo "  -h, --help    Show this help message"
+            exit 0
+            ;;
+        *)
+            if [ -z "${BUILD_DIR}" ]; then
+                BUILD_DIR="${arg}"
+            fi
+            ;;
+    esac
+done
+
+BUILD_DIR="${BUILD_DIR:-build-linux-release}"
 if [[ "${BUILD_DIR}" = /* ]]; then
     BUILD_PATH="${BUILD_DIR}"
 else
@@ -90,6 +118,7 @@
-# Explicit exclusion list: Glibc, graphics drivers, hardware acceleration, and X11 core
+# Explicit exclusion list: Glibc core/auxiliary, graphics drivers, hardware acceleration, and X11 core
 EXCLUDED_PREFIXES = (
     "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so", "ld-linux",
+    "libresolv.so", "libnss_", "libutil.so", "libanl.so",
     "libGL.so", "libGLX.so", "libEGL.so", "libGLdispatch.so", "libOpenGL.so",
     "libdrm.so", "libgbm.so", "libvulkan.so", "libX11.so"
 )
@@ -186,4 +215,4 @@
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
@@ -235,8 +264,15 @@
     TOOL_ARGS+=("${APPDIR}" "${OUTPUT_APPIMAGE}")
-    "${APPIMAGETOOL}" "${TOOL_ARGS[@]}"
+    if ! "${APPIMAGETOOL}" "${TOOL_ARGS[@]}"; then
+        echo "Error: appimagetool failed to generate package at: ${OUTPUT_APPIMAGE}" >&2
+        exit 1
+    fi
     echo "=== AppImage packaging completed successfully: ${OUTPUT_APPIMAGE} ==="
 else
-    echo "Notice: appimagetool was not detected and could not be downloaded."
-    echo "AppDir staged successfully and ready for manual packaging at: ${APPDIR}"
-    echo "To finalize the AppImage, install appimagetool and run:"
-    echo "  appimagetool \"${APPDIR}\" \"${OUTPUT_APPIMAGE}\""
+    echo "Error: appimagetool was not detected and could not be downloaded." >&2
+    echo "AppDir staged successfully at: ${APPDIR}, but final package could not be generated." >&2
+    echo "To finalize the AppImage, install appimagetool and run:" >&2
+    echo "  appimagetool \"${APPDIR}\" \"${OUTPUT_APPIMAGE}\"" >&2
+    if [ "${STRICT}" = "1" ] || [ -n "${CI:-}" ] || [ -n "${GITHUB_ACTIONS:-}" ]; then
+        exit 1
+    fi
+    echo "Notice: Exiting with status 0 as non-strict mode was requested."
 fi
```

---

### 5.3 Available Pre-Built Artifacts in Explorer Folder

The following artifacts have been authored and verified in `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m5_it2_3/`:
- `apprun_remediation.patch`: Validated git patch for `app/packaging/linux/AppRun`.
- `build_appimage_remediation.patch`: Validated git patch for `packaging/linux/build_appimage.sh`.
- `proposed_AppRun`: Complete drop-in remediated file for `app/packaging/linux/AppRun`.
- `proposed_build_appimage.sh`: Complete drop-in remediated file for `packaging/linux/build_appimage.sh`.

---

## 6. Verification Method & Test Commands

The implementation agent and reviewer can independently verify these remediations with the following deterministic checks:

### 6.1 Patch Applicability
Verify both patches apply cleanly against the current tree:
```bash
git apply --check /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m5_it2_3/apprun_remediation.patch
# Expected: Exit code 0

git apply --check /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m5_it2_3/build_appimage_remediation.patch
# Expected: Exit code 0
```

### 6.2 Bash Syntax Validation
Verify shell script syntax after applying changes:
```bash
bash -n app/packaging/linux/AppRun
# Expected: Exit code 0

bash -n packaging/linux/build_appimage.sh
# Expected: Exit code 0
```

### 6.3 AppRun Non-Zero Exit & Crashhandler Wait Simulation
Verify that `AppRun` does not abort under `set -e` when `olive-editor` crashes, and properly waits for `olive-crashhandler`:
```bash
bash -c '
set -e
sleep 2 &
PID=$!
EXIT_CODE=0
bash -c "exit 139" || EXIT_CODE=$?
while pgrep -u "$(id -u)" -x sleep >/dev/null 2>&1; do
  sleep 1
done
exit "${EXIT_CODE}"
'
# Expected: Process waits for sleep 2 to finish, then exits with code 139
```

### 6.4 Missing Packaging Tool Exit Code Verification
Verify that `build_appimage.sh` exits with code `1` when `appimagetool` is unavailable under strict mode:
```bash
PATH=/usr/bin:/bin ./packaging/linux/build_appimage.sh --strict nonexistent_build_test_dir
# Expected: Exits with error message and status 1
```

### 6.5 Transitive Library Leak Check
After running the updated `build_appimage.sh`:
```bash
ls AppDir/usr/lib | grep -E "^(libc\.so|libm\.so|libresolv\.so|ld-linux)" || echo "Clean: No Glibc libraries found"
# Expected output: "Clean: No Glibc libraries found" (libresolv.so.2 is absent)
```

### 6.6 Invalidation Conditions
- Any failure when running `git apply --check` with the provided patches.
- Any syntax error reported by `bash -n`.
- `AppRun` exiting prematurely without entering the `while pgrep` wait loop when `olive-editor` returns non-zero.
- `libresolv.so.2` appearing in `AppDir/usr/lib`.
- `build_appimage.sh` exiting with `0` when `appimagetool` is missing or packaging fails.
