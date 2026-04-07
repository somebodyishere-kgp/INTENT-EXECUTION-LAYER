# IEE v1.7 AI SDK and Task Interface

## Purpose
IEE v1.7 introduces an additive AI-facing interface layer for deterministic planning and execution.

This layer is composed of:

- `IEEClient` for stateless state retrieval and execution
- `TaskRequest` + `TaskPlanner` for planning-only task decomposition
- `AIStateView` for compact model-facing state projection
- `ExecutionContract` + `RevealExecutor` for reveal-aware guaranteed execution

## SDK Surface

Header: `interface/sdk/include/IEEClient.h`

Example usage source:

- `interface/sdk/examples/ai_client_example.cpp`

Primary API:

- `EnvironmentState IEEClient::GetState()`
- `std::string IEEClient::GetStateAiJson()`
- `ClientExecuteResponse IEEClient::Execute(const ClientExecuteRequest& request)`

`ClientExecuteRequest` supports:

- intent action (`activate`, `set_value`, `select`, `create`, `delete`, `move`)
- UI/file targets
- optional `nodeId` for reveal-aware contract execution
- timeout/retry/verification controls

`ClientExecuteResponse` returns:

- normalized `Intent`
- `ExecutionContractResult` with reveal and verification status

## Task Interface

Header: `core/interaction/include/TaskInterface.h`

Task primitives:

- `TaskRequest`
- `TaskPlanCandidate`
- `TaskPlanResult`
- `TaskPlanner`

Planner behavior:

- deterministic node ordering and scoring
- optional hidden-node planning
- domain bias (`generic`, `presentation`, `browser`)
- stable `task_id` derived from request + graph version

## API Additions

New routes:

- `GET /state/ai`
- `POST /task/plan`

Updated route:

- `GET /perf?strict=true` now includes strict pass/fail metrics and returns `409 Conflict` when strict mode fails.

## Execution Guarantee Layer

Headers:

- `core/execution/include/RevealExecutor.h`
- `core/execution/include/ExecutionContract.h`

Contract flow:

1. Resolve node metadata (when `nodeId` is provided)
2. Execute reveal strategy if required
3. Execute intent through `ExecutionEngine`
4. Enforce verification outcome

When verification fails, contract status is marked failed even if adapter dispatch completed.

## CLI Quickstart

```powershell
# inspect strict performance contract
./build/Debug/iee.exe perf --target_ms 16 --strict

# deterministic planning demos
./build/Debug/iee.exe demo presentation
./build/Debug/iee.exe demo browser --json

# optional execution attempt via contract
./build/Debug/iee.exe demo presentation --run
```

## API Quickstart

```powershell
# start local API
./build/Debug/iee.exe api --port 8787
```

Task planning payload:

```json
{
  "goal": "export hidden menu",
  "target": "Export",
  "domain": "presentation",
  "allow_hidden": "true",
  "max_plans": "3"
}
```

Sample endpoints:

```text
GET  /state/ai
POST /task/plan
GET  /perf?target_ms=16&strict=true
```
