# IEE v3.2.1 Reflex Runtime (Phase 15)

## Purpose

This document describes the coordinated continuous URE runtime in v3.2.1.

v3.0 added step-mode URE.
v3.1 added continuous runtime integration.
v3.2.1 adds multi-intent bundle coordination, continuous control smoothing, richer diagnostics, and disk-backed runtime memory.

## Runtime Model

Continuous URE flow:

1. ControlRuntime captures synchronized environment state.
2. UreDecisionProvider runs UniversalReflexAgent::Step(...).
3. Runtime builds attention map and predicted states from world model.
4. Specialist agents propose bundles in parallel (movement, aim, interaction, strategy).
5. Meta-policy bundle is merged and refined by MicroPlanner.
6. ActionCoordinator resolves conflicts and fuses continuous/discrete outputs.
7. ContinuousController smooths final vector output.
8. Coordinated output maps to runtime intents (discrete + continuous).
9. Intents enqueue non-blocking into ControlRuntime.
10. Execution observer records outcomes to reflex memory and skill memory.

## Endpoint Contract

### Start

POST /ure/start

Supported fields:

- execute: true|false
- priority: auto|high|medium|low
- decision_budget_us
- targetFrameMs, maxFrames, observationIntervalMs, decisionBudgetMs
- demo_mode

### Stop

POST /ure/stop

Stops URE decision provider and execution observer bindings, then persists goal/experience/skill state.

### Status

GET /ure/status

Returns:

- runtime lifecycle flags
- loop counters and timing diagnostics
- goal and goal version
- attention snapshot
- prediction snapshot
- latest bundles and coordinated_output
- skill-memory summaries
- reflex metrics and telemetry envelope
- control runtime status when available

### Goal

POST /ure/goal and GET /ure/goal

Supports:

- goal/objective string
- target/target_hint
- domain
- preferred_actions as array or string
- active bool
- clear/reset bool

### New Coordination Diagnostics

- GET /ure/bundles
- GET /ure/attention
- GET /ure/prediction

### Step and Demo Extensions

POST /ure/step and POST /ure/demo now return:

- attention
- prediction
- bundles
- coordinated_output
- execution result fields (when requested)

## Priority and Intent Mapping

Priority remains derived from reflex decision + runtime priority policy.

Coordinated outputs are mapped as:

- each discrete coordinated action -> intent
- continuous vector (move/aim/look/fire/interact) -> continuous intent payload

This allows multi-intent emission in one pass while keeping queue-based non-blocking execution.

## Persistence Model

Runtime persistence files:

- artifacts/reflex/goal_state_v3_2.json
- artifacts/reflex/experience_state_v3_2.tsv
- artifacts/reflex/skills_v3_2.tsv

Lifecycle:

- load during API server initialization
- save on goal updates and runtime stop paths

## CLI Surfaces

- iee ure live
- iee ure debug --bundles --continuous
- iee ure demo realtime

Debug surfaces expose coordination diagnostics directly through API pass-through.

## Safety and Determinism Guarantees

- policy-gated execution remains enforced
- deterministic ordering and bounded output limits are preserved
- no LLM dependency in reflex runtime
- continuous runtime remains additive and backward-compatible with existing contracts
