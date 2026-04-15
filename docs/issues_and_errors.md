# Issues and Errors Log (v3.2.1 Upgrade)

## Date
2026-04-15

## Resolved During v3.2.1 Migration

### 1. Phase 15 required richer goal payload parsing than flat key/value extraction
- Symptom: incoming object payloads with array-based preferred_actions were not guaranteed by the prior flat parser path.
- Root cause: v3.1 parser model primarily targeted flat string payload extraction.
- Fix: added ParseGoalPayload with explicit string/bool/array extraction fallbacks and clear error paths.
- Result: /ure/goal now supports richer payload schema while preserving v3.1 compatibility.

### 2. Continuous runtime lacked dedicated surfaces for coordination diagnostics
- Symptom: bundle/attention/prediction outputs were not queryable as first-class runtime diagnostics.
- Root cause: runtime status schema had no dedicated coordination endpoints.
- Fix: added GET /ure/bundles, GET /ure/attention, GET /ure/prediction and extended status/demo payloads.
- Result: Phase 15 state can be inspected deterministically via API and CLI.

### 3. Runtime memory did not survive process restart
- Symptom: goal, experience, and skill adaptations reset after service restart.
- Root cause: state was process-local only in v3.1.
- Fix: added restore/persist hooks with bounded experience replay and skill store serialization.
- Result: runtime memory is now disk-backed across restarts.

### 4. Single-intent runtime path under-represented coordinated specialist outputs
- Symptom: continuous loop could not emit coordinated multi-action output bundles.
- Root cause: decision provider mapped only one reflex decision to one intent per pass.
- Fix: added specialist bundle generation, micro-planner refinement, action coordination, and discrete/continuous intent mapping.
- Result: coordinated outputs are now emitted and observable in realtime runs.

### 5. CMake extension configure diagnostics remained unreliable in this environment
- Symptom: VS Code CMake extension intermittently reported configure failure without actionable diagnostics.
- Root cause: environment/tooling issue outside project source correctness.
- Fix: used direct cmake/cmake-build/ctest commands as source-of-truth validation path.
- Result: release validation remains reliable despite extension instability.

## Current Known Risks (Non-Blocking)

### 1. Persistence artifacts can pollute git state if not ignored
- Current behavior: goal/experience/skills files are created under artifacts/reflex during runtime.
- Risk: local runtime state may appear as unreviewed release diffs.

### 2. Continuous vectors currently flow through generic intent params
- Current behavior: adapters consume vector values through intent params rather than dedicated analog contract types.
- Risk: adapter portability is good, but native control precision can be improved.

### 3. Prediction model is deterministic and short horizon only
- Current behavior: predictions are center-velocity extrapolations with bounded confidence heuristics.
- Risk: complex non-linear motion remains approximate by design.
