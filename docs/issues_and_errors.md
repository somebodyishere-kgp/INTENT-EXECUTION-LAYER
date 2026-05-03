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

### 6. Coordinated continuous move intents could not execute through runtime path
- Symptom: UI-targeted move intents from coordinated output failed validation and adapter resolution.
- Root cause: intent move validation was filesystem-only and InputAdapter did not handle move action.
- Fix: expanded move validation for UI continuous params and added native InputAdapter move/fire/interact handling.
- Result: coordinated continuous control output is now executable through existing runtime execution contracts.

## Current Known Risks (Non-Blocking)

### 1. Persistence artifacts can pollute git state if not ignored
- Current behavior: goal/experience/skills files are created under artifacts/reflex during runtime.
- Risk: local runtime state may appear as unreviewed release diffs.

### 2. Continuous vectors currently flow through generic intent params
- Current behavior: InputAdapter consumes vector values through intent params for native cursor/click control.
- Risk: additional adapters may need dedicated high-fidelity implementations for domain-specific analog behavior.

### 3. Prediction model is deterministic and short horizon only
- Current behavior: predictions are center-velocity extrapolations with bounded confidence heuristics.
- Risk: complex non-linear motion remains approximate by design.

## Resolved During v4.0 Phase 16 Integration

### 7. Skill persistence schema had no room for hierarchy metadata
- Symptom: v3.2.1 skill store only persisted name/attempts/sequence fields.
- Root cause: flat TSV schema did not include category/dependency or complexity metadata.
- Fix: extended load/save paths to support category, complexity_level, estimated_frames, dependencies while remaining backward-compatible with old records.
- Result: existing skills load safely and new v4.0 metadata persists deterministically.

### 8. Runtime state lacked slots for anticipation, strategy, and preemption snapshots
- Symptom: no place to expose v4.0 planning signals through /ure/status.
- Root cause: UreRuntimeState was v3.2.1 coordination-only.
- Fix: added ranked skill/hierarchy snapshots, anticipation, strategy, preemption, and per-layer frame counters.
- Result: v4.0 signals are now queryable and testable without changing legacy routes.

### 9. URE API route surface did not include Phase 16 observability endpoints
- Symptom: operators could not inspect active skills, anticipation, or strategy plans.
- Root cause: API only exposed bundles/attention/prediction diagnostics.
- Fix: added GET /ure/skills, /ure/skills/active, /ure/anticipation, and /ure/strategy.
- Result: full v4.0 state is exposed for debug and integration testing.

### 10. Editor diagnostics produced a false parse error in CliApp despite successful compilation
- Symptom: get_errors reported "expected an identifier" in ReadPort while Release build succeeded.
- Root cause: stale/inaccurate editor diagnostic state, not compiler failure.
- Fix: validated with cmake --build build --config Release and retained clean compile behavior.
- Result: build and test pipeline remains the source of truth for C++ validation in this environment.

## Current Known Risks (v4.0, Non-Blocking)

1. CMake extension configure path can still fail intermittently in this environment; command-line build remains reliable.
2. Preemption currently boosts existing bundle priority rather than hard swapping complete execution trees.
3. Strategy synthesis is bounded and deterministic by design; richer long-horizon planning remains future work.
