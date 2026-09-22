# Task Assignment: E2E Testing Orchestrator
Target: /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_e2e
Authoritative request: /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md
Master project: /home/yuri/Documentos/olive/PROJECT.md
Scope: /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_e2e/SCOPE.md
Parent: teamwork_preview_orchestrator_1
Parent Conv ID: 2abb8c8e-0fc7-4809-9b75-0af6692b6370

## 2026-09-20T14:16:14Z
You are the Sub-Orchestrator for the E2E Testing Track (teamwork_preview_suborch_e2e).
Your working directory is: /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_e2e

MANDATORY FIRST STEP: Read the authoritative user request at /home/yuri/Documentos/olive/.agents/ORIGINAL_REQUEST.md before starting work.
Also read the master project document at /home/yuri/Documentos/olive/PROJECT.md and your scope at /home/yuri/Documentos/olive/.agents/teamwork_preview_suborch_e2e/SCOPE.md.

Your parent is teamwork_preview_orchestrator_1 (conversation ID: 2abb8c8e-0fc7-4809-9b75-0af6692b6370).

Your objective:
Orchestrate the design, implementation, and verification of the comprehensive opaque-box E2E test suite according to the 4-tier methodology:
- Tier 1: Feature Coverage (>=5 tests per feature, happy paths)
- Tier 2: Boundary & Corner Cases (>=5 tests per feature, edge values, zeroes, limits)
- Tier 3: Cross-Feature Combinations (pairwise interactions)
- Tier 4: Real-World Application Scenarios (full multi-step workflows)
Cover all features in PROJECT.md Feature Inventory (EQ, Track Audio Mixer, VU Meters, Scene Cut Detection, Timeline Auto-Split, FCPXML, OTIO, Packaging).

Deliverables:
1. `TEST_INFRA.md` at project root (/home/yuri/Documentos/olive/TEST_INFRA.md)
2. Automated test scripts/cases in `tests/e2e/` (opaque-box, runnable via ctest or python runner)
3. `TEST_READY.md` at project root (/home/yuri/Documentos/olive/TEST_READY.md)

You are an orchestrator: decompose into tasks, dispatch workers (e.g., teamwork_preview_test_writer or teamwork_preview_worker), run reviews, ensure 0 leaks/asserts under ASan, and publish TEST_READY.md.
When finished, send your completion report via send_message to parent (conversation ID: 2abb8c8e-0fc7-4809-9b75-0af6692b6370).
