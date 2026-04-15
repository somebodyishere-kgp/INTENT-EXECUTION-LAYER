# Issues and Errors Log (v3.1 Upgrade)

## Date
2026-04-15

## Resolved During v3.1 Migration

### 1. Reflex telemetry merge risked lock recursion in snapshot flow
- Symptom: `TelemetrySnapshot` merge path could deadlock if reflex aggregation were called while telemetry mutex was held.
- Root cause: reflex telemetry aggregation requires telemetry mutex and needed separation from locked snapshot section.
- Fix: moved reflex aggregation call outside the telemetry lock section.
- Result: merged telemetry path is safe under concurrent runtime load.

### 2. `/ure/goal` update path initially double-incremented goal version
- Symptom: goal version increments were inconsistent after updates.
- Root cause: version bump executed both inside and after update branch.
- Fix: consolidated to one increment per goal update operation.
- Result: deterministic goal versioning.

### 3. `/ure/goal` status serialization risked mutex re-entrancy
- Symptom: route attempted to serialize full URE status while holding runtime goal mutex.
- Root cause: `SerializeUreStatusJson()` also locks the same runtime mutex.
- Fix: capture goal snapshot under lock, release lock, then serialize status.
- Result: goal route is safe and lock-order consistent.

### 4. Continuous URE runtime lacked deterministic endpoint regression checks
- Symptom: new runtime endpoints and telemetry route required explicit integration coverage.
- Root cause: previous tests only covered step-mode URE routes.
- Fix: extended `integration_universal_reflex` and `integration_api_hardening` with:
	- `/ure/start`, `/ure/stop`, `/ure/status`, `/ure/goal`, `/telemetry/reflex`
- Result: Phase 14 endpoint contracts are regression guarded.

### 5. CMake Tools workspace integration intermittently reported configure failure
- Symptom: VS Code CMake Tools build/test wrapper reported "Unable to configure the project" despite valid CMake config.
- Root cause: tooling integration issue in session, not source/CMake content.
- Fix: verified with direct `cmake -S . -B build`, `cmake --build`, and `ctest` runs.
- Result: build and test baseline validated independently of tooling wrapper.

## Current Known Risks (Non-Blocking)

### 1. Reflex goal and experience state are process-local
- Current behavior: goal and adaptation state are runtime memory only.
- Risk: state resets on process restart unless persistence layer is added.

### 2. Continuous provider currently emits one reflex intent per decision pass
- Current behavior: single-action output per pass keeps loop bounded.
- Risk: complex coordinated action bundles require multiple passes.

### 3. No domain-specific optimization packs
- Current behavior: inference is intentionally domain-agnostic and structural.
- Risk: domain-tuned precision is lower than specialized packs by design.
