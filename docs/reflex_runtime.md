# IEE v3.1 Reflex Runtime (Phase 14)

## Purpose

This document describes the continuous Universal Reflex Engine (URE) runtime integration added in v3.1.

v3.0 provided step-mode URE (`/ure/step`).
v3.1 adds continuous frame-synced reflex operation inside the control runtime.

## Runtime Model

Continuous URE flow:

1. `ControlRuntime` captures synchronized environment state.
2. `UreDecisionProvider` runs deterministic `UniversalReflexAgent::Step(...)`.
3. Resulting reflex decision is converted to an intent (if executable and enabled).
4. Intent is enqueued with priority hint (`control_priority`).
5. Existing execution pipeline executes the intent asynchronously.
6. Execution observer records outcome back into reflex experience memory.
7. Reflex telemetry sample is written to merged telemetry stream.

## Endpoint Contract

### Start

`POST /ure/start`

Supported flat JSON string fields:

- `execute`: `"true"|"false"`
- `priority`: `"auto"|"high"|"medium"|"low"`
- `decision_budget_us`: decision budget for reflex step
- `targetFrameMs`, `maxFrames`, `observationIntervalMs`, `decisionBudgetMs`: forwarded control runtime config
- `demo_mode`: optional runtime demo flag

### Stop

`POST /ure/stop`

Stops continuous URE provider and detaches runtime execution observer without forcing control runtime shutdown.

### Status

`GET /ure/status`

Returns:

- runtime lifecycle flags (`active`, `control_active`)
- execution mode (`execute_actions`, priority mode)
- runtime counters (`frames_evaluated`, `intents_produced`)
- execution outcome counters (`execution_attempts`, successes/failures)
- last reflex timing and reason
- current goal payload/version
- reflex metrics and merged telemetry snapshot
- control runtime status when available

### Goal

`POST /ure/goal`

Fields:

- `goal` (required unless `clear=true`)
- `target`
- `domain`
- `preferred_actions` (comma/space-separated)
- `active`
- `clear`

`GET /ure/goal` returns current goal object.

## Goal-Conditioned Reflex

`ReflexGoal` is injected into `MetaPolicyEngine::Decide(...)`.

Behavior:

- object scoring receives deterministic boost when goal tokens match object/type/label
- preferred actions bias selected affordance action when available
- decision reason is prefixed with `goal_conditioned_...` for traceability

## Priority Mapping

When `priority=auto`, runtime maps reflex decision priority to queue classes:

- high: `priority >= 0.85`
- medium: `priority >= 0.62`
- low: otherwise

When `priority` is fixed (`high|medium|low`), runtime always uses configured value.

## Telemetry Integration

v3.1 adds reflex telemetry integration to primary telemetry:

- `Telemetry::LogReflexSample(...)`
- `TelemetrySnapshot.reflex` summary
- `GET /telemetry/reflex` stream
- `GET /ure/metrics` includes metrics + telemetry + runtime envelope

## CLI Surfaces

- `iee ure live`
- `iee ure debug`
- `iee ure demo realtime`

These commands invoke API routes in-process for deterministic local runtime control and diagnostics.

## Safety and Determinism Guarantees

- policy-gated execution (`PermissionPolicyStore`)
- no LLM dependency in reflex loop
- bounded decision budgets and deterministic tie-breaking
- non-blocking continuous pipeline via queue-based execution
- additive integration with existing v2/v3 contracts
