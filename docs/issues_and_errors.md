# Issues and Errors Log (v1.1 Upgrade)

## Date
2026-04-06

## Resolved During v1.1 Migration

### 1. API test linkage failure
- Symptom: `integration_api_hardening` failed with unresolved `IntentApiServer` symbols.
- Root cause: API server implementation was linked only into executable target, not core library used by tests.
- Fix: moved `interface/api/src/IntentApiServer.cpp` into `iee_core` target and removed duplicate executable source listing.
- Result: test binary links and executes correctly.

### 2. Strict JSON parser smoke mismatch from shell payload quoting
- Symptom: initial `curl` smoke payloads were rejected as `invalid_json`.
- Root cause: shell quoting produced escaped payload shape inconsistent with strict parser expectations.
- Fix: validated parser with exact payload files and API integration tests using deterministic request construction.
- Result: strict parser behavior confirmed and documented.

### 3. Constructor dependency propagation breaks after telemetry injection
- Symptom: compile failures due outdated constructor signatures in tests and composition roots.
- Root cause: new telemetry dependency was added to `ExecutionEngine`, `IntentRegistry`, `CliApp`, and `IntentApiServer`.
- Fix: updated all call sites in main and tests.
- Result: build stabilized with telemetry wiring across runtime.

### 4. Adapter resolution rigidity in overlapping capability scenarios
- Symptom: first-match adapter resolution created order-only behavior under overlap.
- Root cause: no runtime reliability/latency feedback loop.
- Fix: added registry score model with:
  - rolling EMA success/failure and latency
  - decay weighting
  - deterministic tie-break on registration order
- Result: dynamic and deterministic best-adapter selection now active.

### 5. Missing observability for execution path decisions
- Symptom: hard to audit adapter decisions and execution outcomes.
- Root cause: runtime lacked first-class telemetry traces.
- Fix: added `Telemetry` module and trace id propagation through `ExecutionResult`.
- Result: CLI/API can inspect live execution telemetry and per-adapter metrics.

### 6. Timeout blind spot in execution flow
- Symptom: delayed adapter operations could still report success.
- Root cause: engine did not enforce `constraints.timeoutMs` after execution.
- Fix: timeout gate now marks over-budget executions as `FAILED` with explicit message.
- Result: deterministic failure under delayed execution is now test-covered.

## Current Known Risks (Non-Blocking)

### 1. JSON strictness profile
- Current behavior: parser supports strict top-level object with string values.
- Risk: clients sending nested/typed payloads must adapt or be rejected with `invalid_json`.

### 2. Telemetry persistence
- Current behavior: telemetry is in-memory only.
- Risk: restart loses historical traces; no long-horizon audit trail yet.

### 3. CLI long-target formatting
- Current behavior: long target values still degrade table readability.
- Risk: reduced operator UX in dense environments.

## Environment Notes
- VS Code CMake Tools configure/build helpers were unavailable in this run despite repeated attempts.
- Authoritative verification was completed via direct `cmake` and `ctest` commands.
