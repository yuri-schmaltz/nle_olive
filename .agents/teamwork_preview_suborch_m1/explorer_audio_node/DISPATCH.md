## 2026-09-20T14:17:20Z

You are explorer_audio_node (type: teamwork_preview_explorer).
Your working directory is: /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_node

MANDATORY FIRST STEP: Read the authoritative user request at /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.
Also read /home/yuri/Documentos/olive/PROJECT.md, /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/SCOPE.md, and the survey at /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_1/survey_audio.md.

TASK:
Perform deep technical codebase exploration to design the Parametric Equalizer Node for Olive:
1. Examine app/node/audio/ (especially volume/volume.h, volume.cpp, pan/pan.h, pan.cpp, and CMakeLists.txt).
2. Examine app/node/factory.h, factory.cpp, app/node/node.h, app/node/value.h, app/render/job/samplejob.h, and ext/core/include/olive/core/render/samplebuffer.h.
3. Design the exact architecture for EqualizerNode:
   - File locations: app/node/audio/equalizer/equalizer.h, equalizer.cpp, and biquad filter header/helper if needed (e.g. biquad.h).
   - Mathematical implementation of Robert Bristow-Johnson Audio EQ Cookbook cascaded biquads in pure C++17 on olive::core::SampleBuffer (planar float).
   - Filter types needed: Low Shelf, High Shelf, Peaking Bell, Low Pass, High Pass, Notch.
   - Frequency clamping (10 Hz to 0.49 * Fs), Q factor (0.1 to 10.0), Gain (-24 dB to +24 dB).
   - Number of bands, band parameters (enable, type, freq, gain, Q) and their NodeValue types.
   - Evaluation inside Node::Value() (static handling) and SampleJob (animated handling if applicable).
   - Registration in NodeFactory (kAudioEqualizer enum in factory.h, instantiation in factory.cpp).
   - Menu categorization and node flags (kCategoryFilter, kAudioEffect, etc.).
   - Memory and thread-safety: zero dynamic allocations in inner audio processing loop.

Write your complete findings and implementation blueprint to:
/home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_node/report.md
When finished, send a completion message to parent orchestrator.
