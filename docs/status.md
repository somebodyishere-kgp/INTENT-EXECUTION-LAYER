# IEE v1.2 Status

## Date
2026-04-06

## Current State
IEE has been upgraded to v1.2 runtime state: continuous control-loop execution, cache invalidation v2, telemetry persistence with rotation, control API endpoints, deterministic input adapter fallback, and expanded high-frequency testing.

## Completed v1.2 Work
- Control runtime subsystem:
  - new `ControlRuntime` with configurable frame budget and optional max-frame cap
  - priority-aware queueing (`High`, `Medium`, `Low`) and event-priority processing
  - runtime status/snapshot/summary serialization support
- Execution engine hardening:
  - new budgeted execution path (`ExecuteWithBudget`)
  - cumulative timeout enforcement across retries
  - fast-path cache v2 with parameter hash + snapshot version matching
  - LRU-style fast-path eviction
- Intent/registry cache safety:
  - `Context` metadata expanded (`snapshotVersion`, `controlFrame`)
  - registry cache key expanded with `cacheEpoch` for explicit invalidation v2
- Adapter coverage:
  - new `InputAdapter` fallback using deterministic `SendInput` keyboard/mouse primitives
- Telemetry v2:
  - async persistence queue for traces
  - rotating JSONL files (`artifacts/telemetry`)
  - persistence status surfaced in snapshot/CLI/API
  - trace metadata now includes snapshot/control-frame proof fields
- API v2 endpoints:
  - `GET /control/status`
  - `POST /control/start`
  - `POST /control/stop`
  - `GET /telemetry/persistence`
  - `POST /execute` supports queued/realtime mode with priority tags
- CLI observability extensions:
  - telemetry filtering by status/adapter/limit
  - persistence inspection mode
- Test expansion:
  - `integration_control_runtime` added
  - `stress_execution_loop` upgraded to 1000-cycle latency percentile assertions (p50/p95/p99)
  - API integration extended to control endpoints

## Verification Executed
- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Debug`
- Automated tests:
  - `ctest --test-dir build -C Debug --output-on-failure`
  - Result: 10/10 passing tests

## Remaining Non-Blocking Gaps
- API JSON parser remains intentionally strict (flat, string-valued payload fields).
- Control runtime queue currently executes one queued intent per cycle by design; batching policy is deferred.
- CMake Tools VS Code build/test helpers still fail to configure in this environment; authoritative verification was run via direct CMake/CTest commands.

## Tooling Note
- VS Code CMake Tools build/test helpers are still unavailable in this session due configure-tool failure with empty diagnostics.
- Validation was completed through direct `cmake` + `ctest` execution and all tests passed.
