## 2026-09-20T14:18:00Z
You are Explorer 2 for Milestone M3 (Async Scene Cut Detection & Timeline Auto-Split).
Your working directory is: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_2

MANDATORY FIRST STEP: Read the authoritative user request at /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md.
Also read:
- /home/yuri/Documentos/olive/PROJECT.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m3/SCOPE.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_2/survey_scenecut.md

Your role & focus:
Investigate and design SceneCutTask inheriting olive::Task (app/task/scenecut/scenecuttask.h and .cpp) and its integration into TaskManager.
Specific technical questions to answer:
1. How olive::Task works in Olive (app/task/task.h, app/task/taskmanager.h). How Start(), Run(), ProgressChanged(double), Cancel(), and IsCancelled() are implemented and invoked.
2. Setting up a dedicated headless CPU FFmpeg decoding session (avformat_open_input, avcodec_find_decoder, avcodec_alloc_context3, avcodec_open2, av_read_frame, avcodec_send_packet, avcodec_receive_frame) that completely avoids DecoderCache and RenderManager to guarantee 0 playback stutter/contention.
3. Seeking accurately to clip->media_in() and decoding up to clip->media_out(), mapping FFmpeg frame presentation timestamps (pts / timebase) to rational media timestamps.
4. Memory safety and RAII under AddressSanitizer: proper deallocation of AVPacket, AVFramePtr (CreateAVFramePtr or av_frame_free), AVCodecContext, AVFormatContext upon completion or early cancellation.
5. Signal emission: Queued signal SceneCutsDetected sending QVector<rational> to the GUI thread.
6. Provide complete header and source code architecture for SceneCutTask ready for the Worker.

Write your complete findings and design report to:
/home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m3_2/report.md
When finished, notify sub-orchestrator (conversation ID: 9582691c-390f-49e1-8b46-cb743a86ad5d) via send_message.
