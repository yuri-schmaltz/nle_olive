# Scope: E2E Testing Track

## Test Philosophy
- Requirement-driven, opaque-box testing based strictly on ORIGINAL_REQUEST.md.
- Methodology: Category-Partition, Boundary Value Analysis, Pairwise Combinatorics, Real-World Workload.
- Publishes TEST_READY.md at project root when complete.

## Feature Inventory to Cover
1. Parametric Equalizer Node (R1) - Biquad filter responses, frequency ranges, gain, Q, multi-band cascades.
2. Track Audio Controls & Mixer (R1) - Volume faders, pan controls, solo/mute, audio metering ballistics.
3. Asynchronous Scene Cut Detection (R2) - Frame difference, histogram analysis, sensitivity thresholds, cancellation.
4. Timeline Auto-Split (R2) - Multi-point clip splitting, audio/video link preservation, undo/redo fidelity.
5. Final Cut Pro 7 XML Interchange (R3) - Sequence, track, clip in/out, media relinking, transitions, markers.
6. OpenTimelineIO Interchange (R3) - OTIO serialization/deserialization, markers, transition integrity.
7. Linux Packaging (R4) - AppImage structure, runtime libraries, Flatpak manifest validation.

## Target Test Tiers
- Tier 1: Feature Coverage (>=5 test cases per feature, happy paths)
- Tier 2: Boundary & Corner Cases (>=5 test cases per feature, limits, zeroes, extremes)
- Tier 3: Cross-Feature Combinations (pairwise interactions, e.g. EQ on split clips, FCPXML with multi-track audio)
- Tier 4: Real-World Application Scenarios (full project workflows, import -> edit/split -> mix -> export)

## Deliverables
- `TEST_INFRA.md` at project root
- Test runners and automated test scripts/cases in `tests/e2e/`
- `TEST_READY.md` at project root summarizing all tiers and runner commands
