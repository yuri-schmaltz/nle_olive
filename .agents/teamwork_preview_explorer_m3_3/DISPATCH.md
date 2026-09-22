## 2026-09-20T14:18:00Z
You are Explorer 3 for Milestone M3 (Async Scene Cut Detection & Timeline Auto-Split).
Your working directory is: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_3

MANDATORY FIRST STEP: Read the authoritative user request at /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md.
Also read:
- /home/yuri/Documentos/olive/PROJECT.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m3/SCOPE.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_2/survey_scenecut.md

Your role & focus:
Investigate and design Timeline Auto-Split Integration & Automated Unit Tests.
Specific technical questions to answer:
1. Timeline UI integration: Where and how to add the "Auto-Split Scenes..." or "Detect Scene Cuts..." action in Olive's UI (MenuShared, TimelineWidget, or clip context menu). How parameters (threshold, min scene length) should be acquired (e.g. dialog or defaults).
2. Time conversion: Exact formula and code to convert detected media timestamps to timeline sequence time (T_seq = clip->in() + clip->MediaToSequenceTime(T_media)) taking into account speed and reverse playback, filtering cuts outside [clip->in(), clip->out()].
3. Executing BlockSplitPreservingLinksCommand on the main GUI thread via Core::instance()->undo_stack() to split video and linked audio blocks with link preservation and full undo/redo.
4. Designing automated unit tests:
   - tests/task/scenecut-tests.cpp: test synthetic frames (black-white, color shifts, strobe/flash rejection, cancellation, edge cases).
   - tests/timeline/scenecut-split-tests.cpp: test multi-point cut execution, link preservation between video and audio blocks, undo/redo state verification.
   - Integration into tests/CMakeLists.txt via olive_add_test.
5. Provide complete implementation details and test code blueprints ready for the Worker.

Write your complete findings and design report to:
/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_3/report.md
When finished, notify sub-orchestrator (conversation ID: 9582691c-390f-49e1-8b46-cb743a86ad5d) via send_message.
