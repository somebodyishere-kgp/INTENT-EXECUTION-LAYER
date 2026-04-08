# IEE v2.0 AI SDK and Platform Interface

## Purpose
IEE v2.0 keeps the v1.x SDK surfaces stable while adding platform-level deterministic contracts for semantic planning, policy gating, temporal state, sequence/workflow orchestration, and UCP envelopes.

## Primary SDK Surface

Header: `interface/sdk/include/IEEClient.h`

Core calls:

- `EnvironmentState IEEClient::GetState()`
- `std::string IEEClient::GetStateAiJson()`
- `ClientExecuteResponse IEEClient::Execute(const ClientExecuteRequest& request)`

These remain compatibility-stable in v2.0.

## Action Interface Layer (v2.0)

Header: `core/action/include/ActionInterface.h`

Contracts:

- `ActionRequest`
- `TargetResolver`
- `ActionExecutor`
- `RecoveryAttempt`
- `ActionExecutionResult`
- `SelfHealingExecutor`

v2 additions:

- recovery metadata in action result (`recovered`, `recovery_attempts`)
- policy-aware failure path (`policy_denied`)
- execution memory recording for adaptive deterministic ranking

## Platform Layer (New in v2.0)

Header: `core/platform/include/PlatformLayer.h`

### Policy

- `PermissionPolicy`
- `PermissionPolicyStore`
- `PermissionCheckResult`

### Experience Memory

- `SuccessStats`
- `ExecutionMemory`
- `ExecutionMemoryStore`

### Temporal State

- `TemporalStateEngine`
- `StateHistory`
- `StateTransitionInfo`
- `FrameConsistencyMetrics`

### Sequence and Workflow

- `IntentSequence`
- `IntentSequenceExecutor`
- `WorkflowExecutor`

### Semantic Bridge

- `SemanticTaskRequest`
- `SemanticPlanResult`
- `SemanticPlannerBridge`

### UCP

- `SerializeUcpActEnvelope(...)`
- `SerializeUcpStateEnvelope(...)`

## API Additions (v2.0)

New/expanded routes:

- `GET /execution/memory`
- `GET /adapters`
- `GET /state/history`
- `GET /policy`
- `POST /policy`
- `GET /perf/percentiles`
- `GET /perf/frame-consistency`
- `POST /act/sequence`
- `POST /workflow/run`
- `POST /task/semantic`
- `POST /ucp/act`
- `GET /ucp/state`

Retained v1.x core routes:

- `/execute`, `/act`, `/task/plan`, `/state/ai`, `/interaction-graph`, `/interaction-node/{id}`, `/trace/{trace_id}`, `/predict`, `/perf`, `/stream/*`, `/control/*`

## Semantic Request Contract

`POST /task/semantic`

```json
{
  "goal": "click Save then click Save",
  "context": {
    "app": "code",
    "domain": "generic"
  }
}
```

Response includes:

- `semantic.mode` (`task_request` or `intent_sequence`)
- `semantic.sequence_generated`
- deterministic diagnostics string
- generated task request or sequence envelope

## Sequence Contract

`POST /act/sequence` and `POST /workflow/run`

```json
{
  "steps": [
    { "action": "activate", "target": "Save" },
    { "action": "activate", "target": "Save" }
  ]
}
```

Response includes attempted/completed counts, failed step index, trace id, and per-step execution result envelopes.

## UCP Envelope Contract

### Act

`POST /ucp/act`

Response:

```json
{
  "ucp_version": "1.0",
  "operation": "act",
  "result": { "status": "success", "trace_id": "..." }
}
```

### State

`GET /ucp/state`

Response:

```json
{
  "ucp_version": "1.0",
  "operation": "state",
  "state": { "sequence": 42, "interaction_summary": { } }
}
```

## Determinism and Runtime Guarantees

- bounded candidate counts and stable tie-break ordering
- bounded self-healing attempts with fixed strategy order
- no stochastic model dependency in core runtime path
- policy checks are deterministic and centralized

## Validation Baseline

- `cmake --build build --config Release`
- `ctest --test-dir build -C Release --output-on-failure`
- Integration hardening includes v2 route assertions.
