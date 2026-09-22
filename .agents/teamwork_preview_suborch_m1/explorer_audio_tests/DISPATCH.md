## 2026-09-20T14:17:20Z

You are explorer_audio_tests (type: teamwork_preview_explorer).
Your working directory is: /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_tests

MANDATORY FIRST STEP: Read the authoritative user request at /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.
Also read /home/yuri/Documentos/olive/PROJECT.md, /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/SCOPE.md, and the survey at /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_survey_1/survey_audio.md.

TASK:
Perform deep technical codebase exploration of the testing infrastructure for Milestone M1:
1. Examine tests/CMakeLists.txt, tests/testutil.h, and tests/timeline/tempo-tests.cpp (and other audio/node tests).
2. Examine how olive_add_test works, how test runners are generated, how SampleBuffer, AudioParams, and sine waves / test signals are created and validated in tests.
3. Check the build and test environment:
   - Inspect build-linux-asan and scripts/gauntlet.py.
   - Check how tests are compiled and executed with ASan.
4. Design the exact test cases for:
   - tests/node/equalizer-tests.cpp:
     - Flat response verification (bypass / 0 dB gain preserves samples within epsilon).
     - Filter response verification (Low-pass attenuates high freq, High-pass attenuates low freq, Peaking Bell boosts target freq by exact dB factor).
     - Stability verification with unit impulse (no NaN, Inf, or divergence across 48,000 samples).
     - Nyquist & parameter limit boundary cases (clamping frequencies near 0 and Nyquist).
     - Multi-channel planar processing (stereo buffers).
   - tests/timeline/track-audio-tests.cpp:
     - Volume scaling verification (0.0 = silence, 0.5 = -6dB, 1.0 = unity, 2.0 = +6dB).
     - Pan verification (-1.0 full left silences ch1, +1.0 full right silences ch0, 0.0 center).
     - Solo logic verification (Track A soloed -> Track B silenced; both soloed -> both active; neither soloed -> both active).
   - Registration in tests/CMakeLists.txt:
     - olive_add_test(node equalizer-tests tests/node/equalizer-tests.cpp)
     - olive_add_test(timeline track-audio-tests tests/timeline/track-audio-tests.cpp)

Write your complete findings and test specification to:
/home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_tests/report.md
When finished, send a completion message to parent orchestrator.
