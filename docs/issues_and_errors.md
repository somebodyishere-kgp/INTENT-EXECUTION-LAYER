# Issues and Errors Log (v1.6 Upgrade)

## Date
2026-04-07

## Resolved During v1.6 Migration

### 1. Legacy-ID assumptions in tests after hashed stable IDs
- Symptom: tests expected `uia-*` node IDs and failed once stable hashed IDs were introduced.
- Root cause: v1.5.1 deterministic IDs were string-prefix based and test assertions were hardcoded.
- Fix: updated tests to resolve nodes by `ui_element_id` and assert stable identity invariants across frames.
- Result: tests now validate v1.6 identity behavior instead of old ID formatting.

### 2. API node payload insufficient for execution-aware workflows
- Symptom: `/interaction-node/{id}` exposed only node + mapped intent.
- Root cause: v1.5.1 schema did not include execution-aware graph contracts.
- Fix: added `execution_plan`, `reveal_strategy`, and `intent_binding` to node endpoint payload.
- Result: node endpoint now fully supports execution planning clients.

### 3. Missing graph delta transport contract
- Symptom: clients could only fetch full UIG snapshots.
- Root cause: no graph-version history/delta in API layer.
- Fix: added graph history buffer and `delta_since` query support on `/interaction-graph`.
- Result: incremental graph sync path now available with `reset_required` signaling for stale cursors.

## Current Known Risks (Non-Blocking)

### 1. Reveal strategy execution gap
- Current behavior: reveal strategies are planned and serialized.
- Risk: adapter-specific reveal execution guarantees still depend on downstream executor support.

### 2. Bounded graph-history model
- Current behavior: graph delta history is bounded to protect memory.
- Risk: stale clients may require full graph reset when base version expires.

### 3. CLI delta horizon
- Current behavior: CLI delta mode is snapshot-oriented.
- Risk: long-running historical diff workflows should use API delta endpoint instead.

### 4. Flat stream-control parser retained
- Current behavior: stream-control parser remains flat/string-valued by design.
- Risk: nested/typed payload producers still require normalization.
