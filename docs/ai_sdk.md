# IEE v1.9 AI SDK and Task Interface

## Purpose
IEE v1.9 extends the additive AI-facing layer with a deterministic Action Interface Layer (AIL) for one-step natural action execution.

This layer is composed of:

- `IEEClient` for stateless state retrieval and execution
- `TaskRequest` + `TaskPlanner` for planning-only task decomposition
- `PlanScore` for deterministic candidate ranking explanation
- `AIStateView` for compact model-facing state projection
- `ExecutionContract` + `RevealExecutor` for reveal-aware guaranteed execution
- `ActionExecutor` + `TargetResolver` for one-step `action + target (+context)` execution

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

## Task Interface (v1.9, inherited from v1.8)

Header: `core/interaction/include/TaskInterface.h`

Task primitives:

- `TaskRequest`
- `TaskPlanCandidate`
- `PlanScore`
- `TaskPlanResult`
- `TaskPlanner`

Planner behavior:

- deterministic node ordering and scoring
- optional hidden-node planning
- domain bias (`generic`, `presentation`, `browser`)
- stable `task_id` derived from request + graph version
- per-candidate score decomposition:
  - `relevance`
  - `execution_cost`
  - `success_probability`
  - `total`

Planner JSON includes:

- legacy `candidates`
- ranked `plans: [{ plan, score }]`

## Action Interface Layer (v1.9)

Header: `core/action/include/ActionInterface.h`

Action primitives:

- `ActionRequest`
- `ActionContextHints`
- `ActionResolutionCandidate`
- `TargetResolution`
- `TargetResolver`
- `ActionExecutionResult`
- `ActionExecutor`

Resolver behavior (deterministic):

- bounded candidate ranking (`maxCandidates <= 8`)
- weighted score factors:
  - label/fuzzy similarity
  - planner score
  - visibility/reveal cost
  - context affinity (`app`, `domain`)
  - recency/memory signals
- deterministic tie-break ordering and ambiguity reporting

Executor behavior:

1. parse and normalize action semantics
2. resolve target deterministically
3. build intent from resolved UIG node
4. execute through `ExecutionContract` (`reveal -> execute -> verify`)
5. return structured result with `trace_id`, execution status, and candidate diagnostics

## API Additions (v1.9)

Routes:

- `GET /state/ai`
- `POST /act`
- `POST /task/plan`
- `GET /trace/{trace_id}`

`GET /state/ai` filter query options:

- `filter=interactive`
- `filter=visible`
- `filter=relevant`
- `goal=<text>`
- `domain=generic|presentation|browser`
- `top_n=<1..64>`
- `include_hidden=true|false`

`GET /perf?strict=true` now includes:

- strict status fields
- `sample_activation_seeded` when strict mode bootstraps an empty sample window

`POST /act` request body:

```json
{
  "action": "activate",
  "target": "Command Palette",
  "value": "",
  "context": {
    "app": "code",
    "domain": "generic"
  }
}
```

`POST /act` response highlights:

- `status` (`success`/`failure`)
- `trace_id`
- `resolved_node_id`
- `plan_used`
- `reveal_used`
- `verified`
- `reason` (on failure)
- `execution` contract summary
- `candidates` (especially for ambiguity diagnostics)

## Execution Guarantee Layer

Headers:

- `core/execution/include/RevealExecutor.h`
- `core/execution/include/ExecutionContract.h`

Contract flow:

1. Resolve node metadata (when `nodeId` is provided)
2. Execute reveal strategy if required
3. Execute intent through `ExecutionEngine`
4. Enforce verification outcome

v1.8 reveal metadata includes:

- `reveal_total_step_attempts`
- `reveal_fallback_used`
- `reveal_fallback_step_count`
- execution-level `used_fallback`

## CLI Quickstart (v1.9)

```powershell
# pure JSON machine mode
./build/Release/iee.exe state/ai --pure-json

# deterministic planning demos with score decomposition
./build/Release/iee.exe demo presentation --pure-json
./build/Release/iee.exe demo browser --pure-json

# pure-json execute result
./build/Release/iee.exe execute create --path notes.txt --pure-json

# strict perf snapshot
./build/Release/iee.exe perf --strict --json

# one-step action interface
./build/Release/iee.exe act "open command palette" --pure-json
./build/Release/iee.exe act --action set_value --target "search bar" --value "github copilot" --domain browser --json
```

## API Quickstart (v1.9)

```powershell
# start local API
./build/Release/iee.exe api --port 8787
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
POST /act
GET  /state/ai?filter=relevant&goal=export%20menu&domain=presentation&top_n=5
POST /task/plan
POST /execute
GET  /trace/{trace_id}
GET  /perf?strict=true
```
