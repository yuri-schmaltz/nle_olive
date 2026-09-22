# BRIEFING — 2026-09-20T18:45:00Z

## Mission
Conduct code investigation and design native C++17/Qt6 FCP7 XML (xmeml v4/v5) import and export engine (`LoadFCPXMLTask` & `SaveFCPXMLTask` in `app/task/project/fcpxml/`).

## 🔒 My Identity
- Archetype: explorer
- Roles: investigation, synthesis
- Working directory: /home/yuri/Documentos/olive/.agents/teamwork_preview_explorer_m4_1_rep
- Original parent: 1f1c3fe7-601f-4066-a5db-4f3593b3394d
- Milestone: M4 - Interchange & Packaging (FCP7 XML Engine Architecture)

## 🔒 Key Constraints
- Read-only investigation — do NOT implement production code directly
- Write all findings, blueprints, and reports inside working directory
- Use QXmlStreamWriter and QXmlStreamReader (zero external dependencies)
- Fully detailed blueprints for implementers
- Self-contained handoff.md with 5 components

## Current Parent
- Conversation ID: 1f1c3fe7-601f-4066-a5db-4f3593b3394d
- Updated: 2026-09-20T18:45:00Z

## Investigation State
- **Explored paths**:
  - `app/node/project/sequence/sequence.h`, `.cpp`
  - `app/node/output/track/track.h`, `.cpp`, `tracklist.h`
  - `app/node/block/clip/clip.h`, `.cpp`
  - `app/node/block/gap/gap.h`
  - `app/node/block/transition/transition.h`, `.cpp`, `crossdissolvetransition.h`
  - `app/node/project/footage/footage.h`, `.cpp`
  - `app/timeline/timelinemarker.h`, `.cpp`
  - `app/task/project/loadotio/loadotio.cpp` & `saveotio/saveotio.cpp`
  - `app/task/project/load/load.cpp` & `save/save.cpp`
  - `ext/core/include/olive/core/util/timecodefunctions.h` & `timecodefunctions.cpp`
- **Key findings**:
  - Exact rational to `<timebase>` + `<ntsc>` conversion formula preserves standard broadcast/film frame rates without roundoff error.
  - Olive tracks are 100% contiguous; empty timeline intervals are explicit `GapBlock` nodes, whereas FCP7 XML gaps are implicit.
  - Dual `<link>` elements require a coordinate pre-pass on export and a deferred `Node::Link()` resolution pass on import.
  - Transitions map to `CrossDissolveTransition` with `in_offset` and `out_offset` preserving alignment.
  - Thread safety requires `project_->moveToThread(qApp->thread())` prior to returning `true`.
- **Unexplored areas**: None. Architecture and design are complete and verified against test suite contracts.

## Key Decisions Made
- Fully designed `SaveFCPXMLTask` and `LoadFCPXMLTask` in `app/task/project/fcpxml/` using pure `QXmlStreamWriter` / `QXmlStreamReader`.
- Formulated exact bidirectional rate conversion algorithms.
- Established implicit gap handling and dual `<link>` synchronization.
- Produced detailed `report.md` and 5-component `handoff.md`.

## Artifact Index
- DISPATCH.md — Incoming dispatch record
- BRIEFING.md — Persistent working memory
- progress.md — Liveness heartbeat
- report.md — Comprehensive architecture and implementation blueprint
- handoff.md — Self-contained 5-component handoff
