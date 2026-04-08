# IEE v2.0 Status

## Date
2026-04-08

## Current State
IEE has been upgraded to v2.0 (Phase 11 platformization) with additive architecture changes over v1.9.

The runtime now supports:

- deterministic self-healing execution attempts
- temporal unified-state history and frame consistency
- sequence and workflow execution endpoints
- semantic task bridge for deterministic semantic-to-plan conversion
- execution memory for node-level success biasing
- adapter metadata discovery for ecosystem clients
- expanded hybrid perception output fields
- policy-gated execution controls
- UCP action/state envelopes
- latency percentile and frame coherency telemetry APIs

## Completed v2.0 Work

### Core platform module
- Added `core/platform` with:
  - `PermissionPolicyStore`
  - `ExecutionMemoryStore`
  - `TemporalStateEngine`
  - `IntentSequenceExecutor`
  - `WorkflowExecutor`
  - `SemanticPlannerBridge`
  - UCP envelope serializers

### Action interface upgrades
- Added recovery contracts (`RecoveryAttempt`, `recovered`, `recoveryAttempts`).
- Added `SelfHealingExecutor` for bounded deterministic recovery.
- Added policy checks and execution-memory recording in `ActionExecutor::Act`.

### Execution and adapter ecosystem
- Added `AdapterMetadata` contract.
- Added metadata listing in `AdapterRegistry` and forwarding in `ExecutionEngine`.

### Perception and telemetry upgrades
- Added lightweight text/grouping region metrics to environment perception.
- Added latency percentile snapshots (`p50/p95/p99/p999`).

### API platformization
- Added and wired v2 routes:
  - `GET /execution/memory`
  - `GET /adapters`
  - `GET /state/history`
  - `GET /policy`, `POST /policy`
  - `GET /perf/percentiles`
  - `GET /perf/frame-consistency`
  - `POST /act/sequence`
  - `POST /workflow/run`
  - `POST /task/semantic`
  - `POST /ucp/act`
  - `GET /ucp/state`

### Testing updates
- Expanded `integration_api_hardening` to assert v2 route behavior:
  - policy update/enforcement
  - sequence/workflow/semantic routes
  - UCP route envelopes
  - adapter metadata and execution memory endpoints
  - percentile and frame consistency endpoints

## Verification

- Build: `cmake --build build --config Release`
- Test: `ctest --test-dir build -C Release --output-on-failure`
- Result: 19/19 tests passed in Release.

## Remaining Non-Blocking Gaps

1. Policy parser currently accepts flat string values only (intentional for deterministic parser scope).
2. Semantic bridge is deterministic-rule based and currently does not call external model runtimes.
3. Execution memory is process-local and not yet persisted across restarts.

## Release Note Summary

v2.0 establishes IEE as a platform layer instead of only an action/runtime layer, while preserving v1.x contracts and deterministic operational boundaries.
