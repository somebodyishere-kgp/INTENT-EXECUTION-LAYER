# IEE Context Handoff (v1.6)

## 1. What Are We Building
The Intent Execution Engine (IEE): a deterministic native execution layer that maps live OS/application state into executable intent space. v1.6 upgrades UIG into a production execution-aware graph with stable identity, node planning semantics, reveal semantics, and versioned deltas.

## 2. Current State
Completed in v1.6:
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
- Reworked `core/interaction` schema to include v1.6 contracts.
- Rebuilt UIG builder logic for stable hash identities and execution-aware node payloads.
- Added graph delta computation and serialization.
- Extended API route handlers with graph history + delta and node execution metadata.
- Added CLI handlers for `plan` and `reveal`, plus graph delta options.
- Updated docs (`architecture`, `status`, `parity`, `issues_and_errors`, `context_handoff`).

## 4. Current Problem
No active blocker identified in the migration patch set.

Known non-blocking considerations:
1. Reveal strategy is currently a planning contract; adapter-level reveal execution can be further hardened.
2. API graph history is intentionally bounded and may return `reset_required` for stale versions.
3. CLI delta mode is snapshot-local and not a long-lived history service.

## 5. Next Plan
1. Integrate reveal strategy execution primitives into adapter execution pipelines.
2. Add long-run graph-soak tests to validate identity stability under fast UI churn.
3. Add optional graph-history cursor/token contracts for long-lived clients.
4. Expand execution-plan validation with adapter capability feasibility checks.

## 6. Key Decisions Taken
- v1.6 remains additive; legacy fields and routes are preserved.
- Stable IDs use deterministic hashing, not UIA string-prefix IDs.
- Descriptor/state split is explicit to support cleaner diff and planning semantics.
- Execution semantics (plan/reveal/binding) are first-class UIG outputs.
- Delta transport uses bounded history plus explicit reset signaling.
