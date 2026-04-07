# IEE Context Handoff (v1.7)

## 1. What Are We Building
The Intent Execution Engine (IEE): a deterministic native execution layer that maps live OS/application state into executable intent space. v1.7 adds an AI-facing interface layer over the v1.6 production execution graph.

## 2. Current State
Completed in v1.7:
- AI SDK surface:
  - added `IEEClient` with stateless state retrieval and contract-backed execute
- Task interface:
  - added `TaskRequest`, `TaskPlanner`, and deterministic plan result contracts
  - added `POST /task/plan` planning-only endpoint
- Reveal hardening and execution guarantee:
  - added `RevealExecutor` with retries + reveal verification
  - added `ExecutionContract` enforcing `reveal -> execute -> verify`
  - upgraded `/execute` responses with contract/reveal stage diagnostics
- AI state projection:
  - added `AIStateView` and `GET /state/ai`
- Latency strict mode:
  - added CLI `iee perf --strict`
  - added API strict metrics in `GET /perf?strict=true`
- Developer experience:
  - added `iee demo presentation|browser [--json] [--run]`
  - added `docs/ai_sdk.md`
- Validation upgrades:
  - added `scenario_task_interface`
  - updated `integration_api_hardening` for `/state/ai`, `/task/plan`, and strict perf

Retained in v1.7 from v1.6:
- Stable node identity:
  - added `NodeId { stableId, signature }`
  - deterministic hash from UI path/role/label/automation context
- Node contract split:
  - added `InteractionDescriptor` (structure)
  - added `InteractionState` (runtime state)
- Execution-aware model:
  - added per-node `ExecutionPlan` and `PlanStep`
  - added `RevealStrategy` for hidden/offscreen/collapsed paths
  - added `NodeIntentBinding` for node-action-plan-reveal binding
- Versioned diff model:
  - added `InteractionGraph.version`
  - added `GraphDelta` compute + serialization
- API upgrades (additive):
  - `/interaction-graph` includes graph version
  - `/interaction-graph?delta_since=<version>` returns graph delta with reset semantics
  - `/interaction-node/{id}` includes execution plan/reveal/binding payloads
  - `/capabilities/full` includes graph version/signature metadata
- CLI upgrades:
  - added `iee plan <id>`
  - added `iee reveal <id>`
  - extended `iee graph` with delta options
- Test upgrades:
  - `unit_interaction_graph`: stable IDs + reveal/plan/delta checks
  - `scenario_uig_hidden_exposure`: hashed-ID-safe hidden node assertions
  - `integration_api_hardening`: dynamic node lookup + graph delta API assertions

Retained from v1.5.1 and earlier:
- Full hidden UIA capture path
- Unified state merge (`ScreenState` + `InteractionGraph`)
- Existing API/CLI route compatibility
- Telemetry/runtime control/streaming behavior

## 3. Last Work Done
- Added new v1.7 modules:
  - `TaskInterface` (`TaskRequest`, `TaskPlanner`)
  - `AIStateView` projector
  - `RevealExecutor`
  - `ExecutionContract`
  - `IEEClient`
- Wired API additions:
  - `GET /state/ai`
  - `POST /task/plan`
  - strict perf fields on `/perf`
  - contract metadata on `/execute`
- Wired CLI additions:
  - `iee demo presentation|browser [--json] [--run]`
  - `iee perf --strict`
- Added scenario and integration test coverage for v1.7 routes and planner behavior.
- Updated README and v1.7 docs sync set.

## 4. Current Problem
No active blocker.

Known non-blocking considerations:
1. Reveal steps currently map to generic activation primitives; adapter-specific reveal actions can be expanded.
2. Task planner ranking is deterministic keyword/domain based; richer semantic ranking is future work.
3. API graph history is intentionally bounded and may return `reset_required` for stale versions.

## 5. Next Plan
1. Expand adapter-native reveal action mappings (`expand_parent`, `scroll_into_view`) for stronger reveal guarantees.
2. Add planner stress tests for large graphs and high-ambiguity label sets.
3. Add optional `TaskRequest` guardrail fields (allowlist/denylist actions, max reveal steps).
4. Extend SDK samples for external process integration and API parity examples.

## 6. Key Decisions Taken
- v1.7 remains additive and non-breaking over v1.6.
- Task planning is planning-only (`/task/plan`) and never executes side effects.
- Execution guarantees are explicit and opt into reveal metadata through node-aware contracts.
- AI state exposure is compact and deterministic (`AIStateView`) instead of full raw snapshots.
- Strict perf mode is explicit (`--strict` / `strict=true`) rather than always enforcing non-zero exits.
