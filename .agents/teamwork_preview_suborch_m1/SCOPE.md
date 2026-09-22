# Scope: Milestone M1 (Audio Engine & Parametric EQ Node)

## Objectives
Implement the native C++17 Parametric Equalizer node and Track-level audio mixing controls (volume, pan, solo).

## Requirements & Scope Boundaries
1. **Parametric Equalizer Node**:
   - Create `EqualizerNode` in `app/node/audio/equalizer/equalizer.h` and `equalizer.cpp`.
   - Implement Robert Bristow-Johnson (RBJ) Audio EQ Cookbook cascaded biquad filters:
     - Low Shelf, High Shelf, Peaking Bell (multiple frequency bands), Low Pass, High Pass.
   - Operates on `olive::core::SampleBuffer` (planar 32-bit floating point).
   - Thread-safe, zero allocations in audio evaluation loop.
   - Register in `NodeFactory` (`kAudioEqualizer`) and expose in audio effects menus.
2. **Track Audio Controls**:
   - Add `kVolumeInput` ("volume_in", float, default 1.0), `kPanInput` ("pan_in", float, default 0.0), and `kSoloInput` ("solo_in", bool, default false) to `Track` (`app/node/output/track/track.h`, `track.cpp`).
   - Evaluate volume and pan in `Track::ProcessAudioTrack`.
   - Support Solo muting logic across tracks in a sequence.
3. **Automated Unit Tests**:
   - Create `tests/node/equalizer-tests.cpp` validating frequency responses, gain adjustments, filter stability, and ASan 0-leak compliance.
   - Create `tests/timeline/track-audio-tests.cpp` validating volume, pan, and solo processing on `SampleBuffer`.
   - Add to `tests/CMakeLists.txt` via `olive_add_test`.
   - Must pass `python3 scripts/gauntlet.py --preset linux-asan --jobs 4`.

## Interface Contracts
- See PROJECT.md § Interface Contracts: EqualizerNode ↔ Track Audio Pipeline.
