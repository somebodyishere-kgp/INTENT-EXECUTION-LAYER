# Issues and Errors Log (v3.0 Upgrade)

## Date
2026-04-15

## Resolved During v3.0 Migration

### 1. New URE integration test failed due invalid enum usage
- Symptom: Release build failed in `integration_universal_reflex.cpp` with missing `UiControlType::Pane`.
- Root cause: test fixture used an enum value that does not exist in the current UI control schema.
- Fix: replaced with valid `UiControlType::Document`.
- Result: new URE integration test compiles and runs.

### 2. API compile warning from route-local symbol shadowing
- Symptom: warning C4457 in `IntentApiServer.cpp` (`request` variable shadowed function parameter).
- Root cause: local `ActionRequest request` in `/ure/step` route reused outer function parameter name.
- Fix: renamed local variable to `reflexRequest`.
- Result: warning removed for new URE route logic.

### 3. API expansion needed deterministic safety gate
- Symptom: reflex route could attempt execution even when policy disabled.
- Root cause: initial route integration did not explicitly model policy denial reason at route level.
- Fix: added explicit policy check and `execution_reason = policy_denied` path.
- Result: reflex execution now strictly obeys policy constraints.

### 4. Route-level URE observability lacked dedicated regression checks
- Symptom: newly added URE routes had no test guard.
- Root cause: v3 route implementation landed before dedicated integration coverage.
- Fix: added `integration_universal_reflex` and CTest registration.
- Result: URE world-model/affordance/decision/metrics/experience/step/demo contracts are regression guarded.

## Current Known Risks (Non-Blocking)

### 1. Reflex loop is API-step based
- Current behavior: URE runs on demand through API routes.
- Risk: frame-by-frame continuous reflex in control runtime loop is not yet enabled.

### 2. Adaptation scope is process-local
- Current behavior: experience memory and failure bias are in-memory.
- Risk: adaptation resets on process restart.

### 3. No domain-specific optimization packs
- Current behavior: inference is intentionally domain-agnostic and structural.
- Risk: domain-tuned precision is lower than specialized packs by design.
