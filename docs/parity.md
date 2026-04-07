# IEE v1.6 Requirement Parity

## Objective Coverage

| Requirement | Expected | Implemented | Status |
|---|---|---|---|
| Stable identity across frames | Deterministic node IDs with stable signatures | Added `NodeId` and deterministic hash-based `stableId` generation | Met |
| Descriptor/state split | Structural descriptor and dynamic runtime state | Added `InteractionDescriptor` + `InteractionState` contracts per node | Met |
| Execution plans for nodes | Every node carries execution plan steps | Added `ExecutionPlan` + `PlanStep` generation for each node | Met |
| Reveal model for hidden UI | Hidden/offscreen/collapsed nodes expose reveal sequence | Added `RevealStrategy` generation with reveal steps | Met |
| Intent binding upgrade | Node intent includes action + plan + reveal | Added `NodeIntentBinding` per node | Met |
| Graph versioning | Graph carries monotonically trackable version | Added `InteractionGraph.version` | Met |
| Graph delta API | Queryable graph diff from previous version | Added `GraphDelta` and `/interaction-graph?delta_since=<version>` | Met |
| API enrichment | Existing UIG routes include new execution-aware metadata | Enriched `/interaction-graph`, `/interaction-node/{id}`, `/capabilities/full` payloads | Met |
| CLI enrichment | Execution-aware graph inspection commands | Added `iee plan`, `iee reveal`, and graph delta options | Met |
| Validation updates | Stability/reveal/plan/delta test coverage | Updated unit/scenario/integration tests for v1.6 behaviors | Met |
| Non-regression | Keep existing routes and behavior compatible | Preserved v1.5.1 routes/shape while adding v1.6 fields | Met |

## Verified Runtime Behaviors
- Graph nodes expose:
  - stable identity
  - descriptor + state
  - execution plan
  - reveal strategy
  - intent binding.
- `GET /interaction-graph` returns graph `version`.
- `GET /interaction-graph?delta_since=<version>` returns deterministic delta payload with reset signaling when needed.
- `GET /interaction-node/{id}` returns `execution_plan`, `reveal_strategy`, and `intent_binding`.
- CLI supports `iee plan`, `iee reveal`, and graph delta options.

## Residual Risks (Non-Blocking)
- Reveal strategies are currently planning metadata, not adapter-guaranteed reveal execution.
- API graph delta history is bounded for memory safety and can require reset.
- CLI graph delta is designed for single-snapshot inspection, not durable history.
