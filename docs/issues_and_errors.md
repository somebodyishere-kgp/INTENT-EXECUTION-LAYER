# Issues and Errors Log (v1.4 Upgrade)

## Date
2026-04-06

## Resolved During v1.4 Migration

### 1. CMake Tools configure/build orchestration failure
- Symptom: `Build_CMakeTools` / `RunCtest_CMakeTools` failed with "Unable to configure the project" and no diagnostics.
- Root cause: environment-specific VS Code CMake Tools integration fault.
- Fix: switched to direct `cmake` + `ctest` command path for authoritative verification.
- Result: full build and test validation completed successfully.

### 2. ControlRuntime symbol-lookup compile conflict
- Symptom: MSVC compile error in `ControlRuntime.cpp` where `ToString(intent.action)` resolved to `ControlRuntime::ToString(ControlPriority)`.
- Root cause: member helper name shadowed global `IntentAction` serializer.
- Fix: explicitly qualified action serialization call (`iee::ToString(intent.action)`).
- Result: `iee_core` compiles cleanly with v1.4 decision-loop changes.

### 3. SSE route behavior in test harness mode
- Symptom: `/stream/live` requires socket push behavior, while `HandleRequestForTesting(...)` is request/response.
- Root cause: test harness bypasses raw socket streaming loop.
- Fix: added deterministic metadata response for `/stream/live` in `HandleRequest(...)` while preserving full SSE behavior in socket request path.
- Result: streaming route is testable and production push path remains available.

## Current Known Risks (Non-Blocking)

### 1. Flat stream payload schema
- Current behavior: stream-control parser accepts flat string-valued JSON objects only.
- Risk: typed/nested payload clients require normalization layer.

### 2. Macro partial completion semantics
- Current behavior: sequence execution is deterministic and stop-on-failure capable but non-transactional.
- Risk: long macros may leave intermediate side effects on failed step.

### 3. Bounded SSE stream session model
- Current behavior: SSE connection emits bounded events per request (`events` cap).
- Risk: long-lived fan-out stream scenarios still require brokered streaming infrastructure.

### 4. IDE tooling parity
- Current behavior: VS Code CMake Tools helper path remains unavailable.
- Risk: build/test ergonomics in IDE remain degraded despite green command-line verification.

## Environment Notes
- Command-line verification remains the authoritative path in this environment.
- Latest validation state: build success + `12/12` test pass.
