# Task Assignment: Survey Interchange (FCPXML/OTIO), Packaging (AppImage/Flatpak), and Test Infrastructure (R3, R4)
Target Report: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3/survey_interchange_packaging.md
Authoritative request: /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md
Project root: /home/yuri/Documentos/olive

## 2026-09-20T14:05:42Z
You are teamwork_preview_explorer_survey_3 (role: Interchange Packaging Explorer).
Your working directory is: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3

MANDATORY FIRST STEP: Read the authoritative user request at /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.

Your task is to conduct an in-depth code survey of the Olive Video Editor codebase (located at /home/yuri/Documentos/olive) focusing on Requirements R3 and R4, plus test and quality infrastructure:
1. Timeline Interchange (FCPXML & OTIO):
   - How does Olive currently import and export projects? What project file format is used?
   - Is there any existing FCP7 XML (xmeml) or OpenTimelineIO (OTIO) support, parser, or serializer in Olive or third-party dependencies?
   - What data structures represent timeline elements (sequences, video/audio tracks, clips, media references, in/out points, transitions, markers, timebase/framerates)?
   - What are the exact requirements to achieve high-fidelity interchange between Olive, Kdenlive, and Premiere using FCP7 XML and OTIO?
2. Linux Packaging Automation (AppImage & Flatpak):
   - What packaging files or build scripts currently exist in the repository (e.g., in packaging/, deploy/, or cmake/)?
   - What are the exact build dependencies (Qt6, FFmpeg, OpenColorIO, OpenImageIO, etc.) and how can a reproducible AppImage script be structured to package all required libraries cleanly?
   - What Flatpak manifest (JSON or YAML) and build recipe is needed for Flatpak distribution?
3. Gauntlet & Quality Gate Infrastructure:
   - Inspect `scripts/gauntlet.py`. How does it work? How does `--preset linux-asan --jobs 4` execute?
   - How are CMake presets, ASan flags, and ctest targets configured?
   - What are the acceptance criteria and how to ensure 0 leaks and 0 assertion failures?

Document your full analysis with exact file paths, schemas, commands, and recommendations in:
/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3/survey_interchange_packaging.md
Also provide a complete handoff.md in your working directory. Report back when finished.
