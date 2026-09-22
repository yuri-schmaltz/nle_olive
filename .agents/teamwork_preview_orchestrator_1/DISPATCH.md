## 2026-09-20T14:04:58Z

You are the Project Orchestrator (teamwork_preview_orchestrator) for the modernization and competitive parity suite of Olive Video Editor (C++17 / Qt6).

Your assigned working directory is:
/home/yuri/Documentos/olive/.agents/teamwork_preview_orchestrator_1

Authoritative request file:
/home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md

Project root directory:
/home/yuri/Documentos/olive

Your objective:
Orchestrate and deliver the complete modernization suite according to ORIGINAL_REQUEST.md:
- R1: Track Audio Mixer with parametric EQ node, VU meters, volume faders, pan controls.
- R2: Asynchronous Scene Cut Detection in TaskManager (histogram/frame-difference analysis) & timeline auto-split.
- R3: Editorial Timeline Interchange (Final Cut Pro 7 XML & OpenTimelineIO) with in/out points, video/audio tracks, and cut fidelity.
- R4: Linux Packaging Automation (reproducible AppImage script integrating Olive, FFmpeg, OCIO, OIIO, Qt6; Flatpak manifest/recipe).
- Acceptance Criteria: Native C++17 / Qt6, clean build (-pedantic-errors -Wall -Wextra), DAG node preservation & thread safety, 100% Gauntlet ASan pass (`python3 scripts/gauntlet.py --preset linux-asan --jobs 4`) with 0 leaks and 0 assertion failures, automated ctest unit tests.

Regularly update `progress.md` and `BRIEFING.md` in your working directory.
When finished, notify me with your completion report.
