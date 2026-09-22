## 2026-09-20T14:17:53Z

You are the FCP7 XML Architecture Explorer (teamwork_preview_explorer_m4_1).
Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_1
Parent conversation ID: 1f1c3fe7-601f-4066-a5db-4f3593b3394d

MANDATORY FIRST STEP: Read /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.
Also read:
- /home/yuri/Documentos/olive/PROJECT.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m4/SCOPE.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_3/survey_interchange_packaging.md

Your Objective:
Conduct a detailed code investigation and design the native C++17/Qt6 Final Cut Pro 7 XML (xmeml version 4/5) import and export engine (`LoadFCPXMLTask` and `SaveFCPXMLTask` in `app/task/project/fcpxml/`):
1. Examine Olive's core timeline and project data structures:
   - `Sequence` (`app/node/project/sequence/sequence.h`, `.cpp`)
   - `Track` (`app/node/output/track/track.h`, `.cpp`)
   - `ClipBlock` (`app/node/block/clip/clip.h`, `.cpp`)
   - `GapBlock` (`app/node/block/gap/gap.h`, `.cpp`)
   - `TransitionBlock` (`app/node/block/transition/transition.h`, `.cpp`)
   - `Footage` (`app/node/project/footage/footage.h`, `.cpp`)
   - `TimelineMarker` (`app/timeline/timelinemarker.h`, `.cpp`)
   - `ProjectSerializer` (`app/node/project/serializer/serializer.h`, `.cpp`)
   - `ProjectLoadTask` / `ProjectSaveTask` (`app/task/project/`)
2. Design `SaveFCPXMLTask`:
   - Class structure inheriting `olive::Task`.
   - Streaming XML generation using `QXmlStreamWriter` with zero external dependencies.
   - Exact mapping from Olive's timeline to FCP7 `<xmeml version="5">`:
     - Sequence parameters (duration, rate `<timebase>` + `<ntsc>`, timecode, sample characteristics).
     - Video & Audio tracks (`<video><track>...`, `<audio><track>...`).
     - Clips (`<clipitem id="...">`, `<start>`, `<end>`, `<in>`, `<out>`, `<duration>`, `<file id="...">`, `<pathurl>`).
     - Dual `<link>` elements connecting audio and video clipitems.
     - Transitions (`<transitionitem>`, `<start>`, `<end>`, `<alignment>`, `<name>Cross Dissolve</name>`).
     - Markers (`<marker>`, `<name>`, `<comment>`, `<in>`, `<out>`).
3. Design `LoadFCPXMLTask`:
   - Streaming XML parsing using `QXmlStreamReader`.
   - Creation of `Project`, `Sequence`, `Track` (video/audio), `ClipBlock`, `GapBlock`, `TransitionBlock`, `TimelineMarker`.
   - Media path resolution (handling `file://` URLs, missing media).
   - Re-establishing clip links (`ClipBlock::block_links()`).
   - Thread safety: explicit transfer `project->moveToThread(qApp->thread())` upon completion before signals are emitted.
4. Deliverables:
   - Write a detailed analysis and implementation blueprint to `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_1/report.md`.
   - Write a self-contained handoff to `/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_1/handoff.md`.
   - Notify parent via `send_message` to 1f1c3fe7-601f-4066-a5db-4f3593b3394d.
