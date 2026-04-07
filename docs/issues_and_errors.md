# Issues and Errors Log (v1.7 Upgrade)

## Date
2026-04-08

## Resolved During v1.7 Migration

### 1. Task planning route bound to wrong endpoint during initial integration
- Symptom: `POST /task/plan` returned route-not-found while integration tests expected 200.
- Root cause: planning handler was accidentally attached to `POST /predict` during patch insertion.
- Fix: corrected route guard to `path == "/task/plan"`.
- Result: task planning endpoint returns deterministic planning-only payloads as intended.

### 2. Task planner action string overload collision
- Symptom: C++ build failed in `TaskInterface.cpp` with conversion error from `IntentAction` to `TaskDomain`.
- Root cause: unqualified `ToString(...)` call inside `TaskPlanner::Plan` resolved to `TaskPlanner::ToString(TaskDomain)`.
- Fix: explicitly called `iee::ToString(node->intentBinding.action)`.
- Result: planner now compiles and emits deterministic action names.

### 3. Intermittent observation pipeline test failure during first full run
- Symptom: `unit_observation_pipeline` failed once with `Expected latest environment state`.
- Root cause: test intermittency/timing sensitivity (non-deterministic repro).
- Fix: reran targeted test and then full suite; test stabilized and full run passed.
- Result: no persistent regression identified from v1.7 changes.

## Current Known Risks (Non-Blocking)

### 1. Reveal action specialization depth
- Current behavior: reveal steps execute through generic activation intents plus fallback.
- Risk: app-specific reveal semantics may still require adapter-specialized reveal actions.

### 2. Deterministic task ranking depth
- Current behavior: task ranking is deterministic keyword/domain based.
- Risk: broader semantic matching for ambiguous goals is intentionally out of scope in v1.7.

### 3. Bounded graph-history model
- Current behavior: graph delta history is bounded to protect memory.
- Risk: stale clients may require full graph reset when base version expires.

### 4. CLI delta horizon
- Current behavior: CLI delta mode is snapshot-oriented.
- Risk: long-running historical diff workflows should use API delta endpoint instead.
