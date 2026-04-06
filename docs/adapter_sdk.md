# IEE Adapter SDK (v1.1)

## 1. Overview
The Adapter SDK defines how external execution backends integrate with IEE. Adapters are responsible for:
- capability discovery
- intent execution
- baseline scoring signals
- optional event subscriptions

The runtime remains deterministic by resolving adapters through score + tie-break rules in `AdapterRegistry`.

## 2. Adapter Interface Contract
Adapters implement the C++ interface in `core/execution/include/Adapter.h`.

Required behavior:
- `Name()` returns a stable adapter identifier.
- `GetCapabilities(...)` emits valid intents for current snapshot and graph context.
- `CanExecute(...)` must be strict and deterministic.
- `Execute(...)` must return structured `ExecutionResult` and never throw across boundary.

v1.1 score and subscription hooks:
- `GetScore()` returns baseline adapter profile:
  - `reliability` in `[0,1]`
  - `latency` in milliseconds
  - `confidence` in `[0,1]`
- `Subscribe(EventBus&)` may attach adapter-local handlers for reactive behavior.

SDK-style aliases are provided (`name`, `getCapabilities`, `execute`, `getScore`, `subscribe`) for extension ergonomics.

## 3. Registration Model
Register adapters through `AdapterRegistry`.

Available APIs:
- `RegisterAdapter(std::shared_ptr<Adapter>)`
- `GetAdapters()`
- `ResolveBest(const Intent&)`

Determinism rules:
- only adapters where `CanExecute(intent) == true` are candidates
- final score is computed from reliability/confidence/latency
- ties are broken by adapter registration order

## 4. Capability Mapping Rules
When emitting intents from `GetCapabilities(...)`, adapters must follow these rules:
1. Every intent must have explicit `action`, `target`, `source`, and bounded confidence.
2. Use stable IDs for capability-derived intents when possible.
3. Keep target type aligned with action family:
   - UI actions (`activate`, `set_value`, `select`) -> `TargetType::UiElement`
   - filesystem actions (`create`, `delete`, `move`) -> `TargetType::FileSystemPath`
4. Emit only capabilities that the adapter can actually execute.
5. Do not emit ambiguous synthetic targets unless adapter can verify them.

## 5. Execution Contract
`Execute(...)` must:
1. validate required parameters for the action
2. attempt operation deterministically
3. set `status`, `verified`, `method`, `message`, and `duration`
4. return `FAILED` for invalid/unsupported requests
5. avoid side effects outside declared target scope

Status semantics:
- `SUCCESS`: action executed and verification passed
- `PARTIAL`: action executed but verification incomplete
- `FAILED`: action failed or timeout gate exceeded

## 6. Error Handling Rules
Adapters must fail loudly and structurally.

Required behavior:
- always return a meaningful `message` on failure
- do not swallow execution errors silently
- avoid throwing uncaught exceptions from adapter boundary
- set `method` to adapter identity for diagnostics

Registry/runtime behavior assumes adapter errors are recoverable and may trigger:
- retry path
- fallback adapter path
- telemetry failure log

## 7. Performance Expectations
Adapter performance targets for real-time readiness:
- baseline execution under normal conditions: `< 100ms`
- capability discovery should avoid expensive full recomputation where possible
- `CanExecute(...)` should be constant-time or near constant-time
- event handlers in `Subscribe(...)` must be lightweight and non-blocking

Scoring implications:
- lower average latency improves adapter selection
- persistent failures reduce runtime reliability
- stale metrics decay over time, so sustained performance matters

## 8. Integration Checklist
Before shipping a new adapter:
1. implement all required adapter methods
2. return stable `Name()` and non-zero baseline `GetScore()`
3. ensure `CanExecute` is strict and deterministic
4. add unit tests for execute success/failure paths
5. verify behavior through `iee telemetry` and `iee trace`
6. validate API `POST /execute` and `POST /explain` flows against emitted capabilities

## 9. Compatibility Notes
- v1 adapters remain compatible; v1.1 score/subscription hooks have defaults.
- new adapters should provide explicit `GetScore()` for reliable selection behavior.
- adapter SDK is intentionally dependency-light: C++20 + existing IEE module contracts.
