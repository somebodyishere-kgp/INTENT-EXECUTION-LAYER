# IEE Adapter SDK (v2.0)

## 1. Overview
The Adapter SDK defines how execution backends integrate with IEE. v2.0 keeps v1.x compatibility and adds adapter metadata discovery for ecosystem-level platformization.

Adapters are responsible for:

- capability discovery
- intent execution
- score and reliability contribution
- metadata publication
- optional event subscriptions

## 2. Adapter Contract

Defined in `core/execution/include/Adapter.h`.

Required methods:

- `std::string Name() const`
- `std::vector<Intent> GetCapabilities(...)`
- `bool CanExecute(const Intent&) const`
- `ExecutionResult Execute(const Intent&)`

Optional/extended methods:

- `AdapterScore GetScore() const`
- `AdapterMetadata GetMetadata() const`
- `void Subscribe(EventBus&)`

SDK aliases remain available (`name`, `getCapabilities`, `execute`, `getScore`, `getMetadata`, `subscribe`).

## 3. v2.0 Metadata Model

New struct:

```cpp
struct AdapterMetadata {
    std::string name;
    std::string version{"1.0"};
    int priority{100};
    std::vector<std::string> supportedActions;
};
```

Runtime/API usage:

- `AdapterRegistry::ListMetadata()`
- `ExecutionEngine::ListAdapterMetadata()`
- `GET /adapters`

Metadata rules:

1. `name` must be stable and deterministic.
2. `version` should reflect adapter compatibility level.
3. `priority` is deterministic ranking metadata, not an imperative override.
4. `supportedActions` should be unique, normalized, and sorted.

## 4. Deterministic Registration and Selection

Registration APIs:

- `Register(std::unique_ptr<Adapter>)`
- `RegisterAdapter(std::shared_ptr<Adapter>)`

Selection APIs:

- `ResolveBest(const Intent&)`
- `ResolveBest(const Intent&, AdapterDecision*)`

Determinism rules:

- candidates must satisfy `CanExecute(intent)`
- score combines reliability/confidence/latency
- ties break by registration order
- fast-path cache is bounded and validated by snapshot metadata

## 5. Execution Contract

`Execute(...)` must:

1. validate required inputs
2. attempt deterministic operation
3. set full `ExecutionResult` (`status`, `verified`, `method`, `message`, `duration`, `attempts`)
4. return `FAILED` for unsupported/invalid requests
5. avoid hidden side effects outside declared intent scope

Status semantics:

- `SUCCESS`: operation and verification succeeded
- `PARTIAL`: operation succeeded with partial verification
- `FAILED`: operation/verification failed

## 6. Reliability and Telemetry Integration

Runtime will call `RecordExecution(...)` with adapter outcomes.

Adapter runtime metrics update:

- reliability EMA
- latency EMA
- confidence
- success/failure counters

These signals feed deterministic adapter selection and telemetry reporting.

## 7. Error Handling Requirements

Adapters should fail structurally and never silently:

- always set meaningful failure `message`
- set `method` to adapter identity
- avoid uncaught exceptions at boundary
- preserve deterministic behavior for identical inputs

## 8. Integration Checklist

Before shipping an adapter:

1. Implement required contract methods.
2. Provide stable `Name()` and realistic `GetScore()`.
3. Provide deterministic `GetMetadata()`.
4. Validate strict `CanExecute(...)` gating.
5. Add unit/integration tests for success and failure paths.
6. Validate API behavior through:
   - `POST /execute`
   - `POST /explain`
   - `GET /adapters`

## 9. Compatibility Notes

- v1.x adapters remain compatible; metadata defaults are provided.
- No external dependency is required beyond C++20 and existing IEE contracts.
- v2.0 metadata is additive and does not break existing adapter code.
