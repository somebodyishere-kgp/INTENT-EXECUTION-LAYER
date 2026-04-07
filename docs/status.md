# IEE v1.6 Status

## Date
2026-04-07

## Current State
IEE has been upgraded to v1.6 with a production execution-aware UIG. The graph now provides stable hashed node identity, descriptor/state separation, per-node execution plans, reveal strategies for hidden UI, and versioned graph delta support.

## Completed v1.6 Work
- Stable node identity:
  - introduced `NodeId { stableId, signature }`
  - deterministic identity hash from UI path/role/label/automation context
- Descriptor/state split:
  - added `InteractionDescriptor` and `InteractionState`
  - preserved legacy node fields for additive compatibility
- Execution-aware nodes:
  - added `ExecutionPlan` and `PlanStep` for each node
  - added `RevealStrategy` for hidden/offscreen/collapsed targets
  - added `NodeIntentBinding` to bind node -> action + plan + reveal
- Graph versioning and delta:
  - added `InteractionGraph.version`
  - added `GraphDelta` computation/serialization
- API expansion (additive):
  - `GET /interaction-graph` now includes graph version
  - `GET /interaction-graph?delta_since=<version>` returns bounded-history delta payload
  - `GET /interaction-node/{id}` now includes execution plan/reveal/binding payloads
  - `GET /capabilities/full` now includes graph version/signature metadata
- CLI expansion:
  - added `iee plan <node_id> [--json]`
  - added `iee reveal <node_id> [--json]`
  - extended `iee graph` with `--delta_since` / `--delta`
- Validation expansion:
  - updated `unit_interaction_graph` for stable IDs + reveal/plan + delta correctness
  - updated `scenario_uig_hidden_exposure` for hashed IDs and reveal checks
  - updated `integration_api_hardening` for dynamic node lookup and delta endpoint checks

## Retained v1.5.1 Behavior
- Full-tree hidden/offscreen/collapsed UI capture retained.
- Unified state merge (`ScreenState` + `InteractionGraph`) retained.
- Existing API/CLI routes remain available.
- Telemetry, runtime control, and stream endpoints remain compatible.

## Verification
- Build and tests should be run via:
  - `cmake -S . -B build`
  - `cmake --build build --config Debug`
  - `ctest --test-dir build -C Debug --output-on-failure`

## Remaining Non-Blocking Gaps
- Reveal strategy remains a deterministic plan contract; adapter-level reveal execution hardening can be expanded.
- Graph history for API delta is intentionally bounded (reset semantics for stale clients).
- CLI graph delta is snapshot-local and not long-lived session history.
