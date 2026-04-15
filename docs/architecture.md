# IEE v3.0 Architecture

## Purpose
IEE v3.0 introduces the Universal Reflex Engine (URE): a deterministic, domain-agnostic reflex intelligence layer that can infer action opportunities from unseen environments without per-step LLM calls.

The architecture remains additive over v2.x and preserves existing execution contracts.

## URE Pipeline

```text
EnvironmentState (ScreenState + UIG)
        |
        v
UniversalFeatureExtractor
        |
        v
WorldModelBuilder
        |
        v
AffordanceEngine
        |
        v
MetaPolicyEngine
        |
        v
UniversalReflexAgent
        |
        +--> ExplorationEngine
        +--> ExperienceMemory
        +--> Optional /act execution
```

## New Core Module

Added:

- `core/reflex/include/UniversalReflexEngine.h`
- `core/reflex/src/UniversalReflexEngine.cpp`

The module includes the following primitives:

- `UniversalFeature`
- `WorldObject`, `WorldModel`, `Relationship`
- `Affordance`
- `PolicyRule`
- `ExplorationResult`
- `ExperienceEntry`
- `UniversalReflexAgent`

## Integration Points

### Runtime state inputs
- Uses `EnvironmentState`, `ScreenState`, and `InteractionGraph` as deterministic input signals.
- No app-specific adapters are required for reflex inference.

### Action integration
- Reflex decisions can be executed through existing `ActionExecutor` (`POST /act` contract path).
- Reveal/verification behavior remains enforced by existing action and execution contracts.

### Policy integration
- URE execution and exploration are gated by `PermissionPolicyStore`.
- Unsafe execution is blocked before invoking actions.

### API integration
Added routes in API server:

- `GET /ure/world-model`
- `GET /ure/affordances`
- `GET /ure/decision`
- `GET /ure/metrics`
- `GET /ure/experience`
- `POST /ure/step`
- `POST /ure/demo`

## Design Rules Enforced

- No heavy ML/training loops in core runtime.
- No LLM call in reflex loop.
- Deterministic sorting/tie-break behavior.
- Bounded relationship generation and exploration.
- Backward-compatible additive integration with v2 routes.

## Performance Model

URE captures the following timing signals per step:

- decision time (microseconds)
- total loop time (microseconds)
- decision budget compliance (`decision_within_budget`)

Aggregated metrics:

- average decision latency
- p95 decision latency
- average loop latency
- over-budget decision count
- exploratory decision count

## Safety Model

Reflex execution model is safety constrained:

1. Build decision candidate from structural signals only.
2. Check policy (`allow_execute`) before action execution.
3. Use bounded exploration only when execution-safe.
4. Record outcomes and bias against repeated failures.

## Validation Baseline

- Build: `cmake --build build --config Release`
- Test: `ctest --test-dir build -C Release --output-on-failure`

Current baseline includes a dedicated URE integration test.
