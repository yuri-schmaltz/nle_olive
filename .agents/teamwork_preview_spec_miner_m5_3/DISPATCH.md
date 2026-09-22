## 2026-09-20T14:17:41Z
You are teamwork_preview_spec_miner_m5_3, a read-only specification investigator.
Your working directory is: /home/yuri/Documentos/olive/.agents/teamwork_preview_spec_miner_m5_3

MANDATORY FIRST STEP: Read /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.
Also read:
- /home/yuri/Documentos/olive/PROJECT.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m5/SCOPE.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3/survey_interchange_packaging.md

OBJECTIVE:
Mine precise requirements and JSON schema for Linux Flatpak packaging targeting KDE Application Runtime 6.8+:
1. Manifest identification: `packaging/flatpak/org.olivevideoeditor.Olive.json`.
   - `app-id`: `org.olivevideoeditor.Olive`
   - `runtime`: `org.kde.Platform`
   - `runtime-version`: `6.8` (or compatible 6.x)
   - `sdk`: `org.kde.Sdk`
   - `command`: `olive-editor`
2. Sandboxing & finish-args permissions:
   - ipc, x11, wayland, pulseaudio, dri, host filesystem access, freedesktop notifications.
3. Cleanup paths:
   - `/include`, `/lib/pkgconfig`, `/share/man`, etc.
4. Dependency modules not bundled in KDE runtime:
   - PortAudio v19
   - Imath v3.1+
   - OpenEXR v3.2+
   - OpenColorIO v2.3+ (with test/app/python build options disabled)
   - OpenImageIO v2.5+ (with test/tool/python build options disabled)
   - Olive Video Editor (building with cmake-ninja, RelWithDebInfo, BUILD_QT6=ON, source pointing to project root).
5. Validation:
   - Must strictly validate against `python3 -m json.tool packaging/flatpak/org.olivevideoeditor.Olive.json`.

OUTPUT:
Write your complete specification and validated JSON manifest template to `/home/yuri/Documentos/olive/.agents/teamwork_preview_spec_miner_m5_3/handoff.md`.
Update your progress in `/home/yuri/Documentos/olive/.agents/teamwork_preview_spec_miner_m5_3/progress.md`.
When finished, send a brief message with your handoff path to your parent orchestrator (teamwork_preview_suborch_m5, conversation ID: d8291db2-3b3d-41ad-a12d-27615886bd25).
