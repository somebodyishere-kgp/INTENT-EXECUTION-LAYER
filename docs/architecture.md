# IEE v3.1 Architecture

## Purpose
IEE v3.1 extends the Universal Reflex Engine (URE) from API-step mode into a continuous control-runtime intelligence loop.

The architecture remains additive over v2.x and preserves existing execution contracts.

## URE Continuous Pipeline

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
        +--> UreDecisionProvider (continuous)
        |       |
        |       +--> Intent(priority_hint) -> ControlRuntime queue
        |       +--> ReflexTelemetrySample -> Telemetry
        |
        +--> ExplorationEngine
        +--> ExperienceMemory
        +--> Optional /act execution (/ure/step)
        |
        +--> ExecutionObserver feedback -> RecordExecutionOutcome
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
- `ReflexGoal`
- `UniversalReflexAgent`

## Control Runtime Integration

v3.1 introduces a continuous URE runtime layer inside control execution:

- `UreDecisionProvider` drives deterministic reflex decisions in control decision passes.
- Control queue priority hints (`control_priority`) are generated from reflex priority and goal context.
- Runtime execution observer updates reflex adaptation state from actual execution outcomes.
- Integration remains non-blocking: actions are enqueued and executed through existing runtime pipeline.

## Integration Points

### Runtime state inputs
- Uses `EnvironmentState`, `ScreenState`, and `InteractionGraph` as deterministic input signals.
- No app-specific adapters are required for reflex inference.

### Action integration
- Reflex decisions can be executed through existing `ActionExecutor` (`POST /act` contract path).
- Reveal/verification behavior remains enforced by existing action and execution contracts.
- Continuous reflex uses control runtime enqueue/execute path and preserves adapter verification contracts.

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
- `GET /ure/status`
- `GET /ure/goal`
- `GET /telemetry/reflex`
- `POST /ure/step`
- `POST /ure/demo`
- `POST /ure/start`
- `POST /ure/stop`
- `POST /ure/goal`

### CLI integration

- `iee ure live`
- `iee ure debug`
- `iee ure demo realtime`

## Goal-Conditioned Reflex

`ReflexGoal` enables deterministic goal conditioning without LLM calls:

- goal text, target hint, domain, preferred action list
- active/inactive switching
- versioned runtime updates through `/ure/goal`
- policy scoring bias when world objects match goal tokens

## Design Rules Enforced

- No heavy ML/training loops in core runtime.
- No LLM call in reflex loop.
- Deterministic sorting/tie-break behavior.
- Bounded relationship generation and exploration.
- Backward-compatible additive integration with v2 routes.
- Continuous path remains non-blocking and bounded per decision pass.

## Performance Model

URE captures timing in both step-mode and continuous-mode:

- decision time (microseconds)
- total loop time (microseconds)
- decision budget compliance (`decision_within_budget`)

Aggregated metrics:

- average decision latency
- p95 decision latency
- average loop latency
- over-budget decision count
- exploratory decision count
- goal-conditioned decision count
- intents produced in continuous mode

Merged telemetry surfaces:

- `TelemetrySnapshot.reflex`
- `GET /telemetry/reflex`
- `GET /ure/metrics` runtime + telemetry envelope

## Safety Model

Reflex execution model is safety constrained:

1. Build decision candidate from structural signals only.
2. Check policy (`allow_execute`) before action execution.
3. Use bounded exploration only when execution-safe.
4. Record outcomes and bias against repeated failures.
5. Continuous runtime can be stopped independently via `/ure/stop` without tearing down control runtime.

## Validation Baseline

- Build: `cmake --build build --config Release`
- Test: `ctest --test-dir build -C Release --output-on-failure`

Current baseline includes URE runtime endpoint checks in integration hardening and universal reflex tests.
