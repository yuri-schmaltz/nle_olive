## 2026-09-20T18:40:19Z

You are worker_m1 (type: teamwork_preview_worker).
Your working directory is: /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/worker_m1

MANDATORY FIRST STEP: Read the authoritative user request at /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.
Also read /home/yuri/Documentos/olive/PROJECT.md and /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/SCOPE.md.
Also read the detailed Explorer blueprints:
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_node/report.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_track_audio/report.md
- /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/explorer_audio_tests/report.md

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A teamwork_preview_auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

EXCLUSIVE FILE WRITE OWNERSHIP:
You own exclusively:
- app/node/audio/equalizer/biquad.h
- app/node/audio/equalizer/equalizer.h
- app/node/audio/equalizer/equalizer.cpp
- app/node/audio/equalizer/CMakeLists.txt
- app/node/audio/CMakeLists.txt
- app/node/factory.h
- app/node/factory.cpp
- app/node/output/track/track.h
- app/node/output/track/track.cpp
- app/node/output/track/tracklist.h
- app/node/output/track/tracklist.cpp
- tests/node/equalizer-tests.cpp
- tests/timeline/track-audio-tests.cpp
- tests/CMakeLists.txt

YOUR TASKS:
1. Implement Parametric Equalizer Node:
   - Create app/node/audio/equalizer/biquad.h implementing Robert Bristow-Johnson Audio EQ Cookbook biquad filters in pure C++17 on planar float buffers. Support Low Shelf, High Shelf, Peaking Bell, Low Pass, High Pass, and Notch. Implement Transposed Direct Form II with zero dynamic memory allocation in the processing loop. Frequency clamping (10 Hz to 0.49 * Fs), Q clamping (0.1 to 10.0), Gain clamping (-24 dB to +24 dB).
   - Create app/node/audio/equalizer/equalizer.h and equalizer.cpp. Inherit olive::Node, declare bands with enable, type, freq, gain, Q inputs. Implement Value() for static buffer processing and ProcessSamples() for keyframed jobs. Expose Category kCategoryFilter, flags kAudioEffect, and SetEffectInput(kSamplesInput).
   - Create app/node/audio/equalizer/CMakeLists.txt and update app/node/audio/CMakeLists.txt (add_subdirectory(equalizer)).
   - Register kAudioEqualizer in app/node/factory.h and app/node/factory.cpp.

2. Implement Track Audio Mixing Controls:
   - Update app/node/output/track/track.h and track.cpp:
     - Add kVolumeInput ("volume_in", float, default 1.0, dB view), kPanInput ("pan_in", float, default 0.0, range -1.0 to 1.0), and kSoloInput ("solo_in", bool, default false) with flags (kInputFlagNotConnectable | kInputFlagNotKeyframable).
     - In Track::ProcessAudioTrack, apply volume and stereo balance pan to block_range_buffer. Ensure safe fallback (if value row doesn't contain volume/pan, fallback to GetStandardValue so standalone tests like tempo-tests.cpp are not silenced).
     - In app/node/output/track/tracklist.h and tracklist.cpp: Add HasSoloTrack() and handle solo state changes.
     - In Track::IsEffectivelyMuted(), return true if explicitly muted OR (sequence has any solo track AND this track is not soloed).
     - In Track::GetActiveElementsAtTime, return ActiveElements::kNoElements if IsEffectivelyMuted().

3. Implement Automated Unit Tests:
   - Create tests/node/equalizer-tests.cpp per explorer_audio_tests/report.md:
     - EqualizerFlatResponse
     - EqualizerFilterResponses (Low-Pass, High-Pass, Peaking Bell boost/cut)
     - EqualizerUnitImpulseStability (48,000 samples, finite, tail decay)
     - EqualizerNyquistAndLimits
     - EqualizerPlanarStereoIndependence
   - Create tests/timeline/track-audio-tests.cpp per explorer_audio_tests/report.md:
     - TrackAudioVolumeScaling
     - TrackAudioStereoPanning
     - TrackAudioSoloLogic
   - Update tests/CMakeLists.txt to register both tests via olive_add_test.

4. Build and Verify:
   - Build the project and test targets using cmake:
     cmake --build build-linux-asan -j 4
   - Run unit tests under ASan:
     ctest --test-dir build-linux-asan -R "equalizer-tests|track-audio-tests|tempo-tests" --output-on-failure
   - Run the gauntlet check:
     python3 scripts/gauntlet.py --preset linux-asan --jobs 4
   - Ensure 100% test pass, 0 memory leaks, 0 assertion errors.

5. Report:
   Write your handoff report to:
   /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_m1/worker_m1/handoff.md
   Document all changes, files touched, commands executed, and test outputs.
   When done, send a message to parent sub-orchestrator.
