# IEE v2.0 Architecture

## Purpose
IEE v2.0 platformizes the v1.x runtime into a deterministic, extensible control platform. The core objective is to keep v1.x execution guarantees intact while adding self-healing, semantic intent mapping, temporal state memory, policy controls, and ecosystem-level adapter metadata.

## v2.0 Platform Layer

New module:

- `core/platform/include/PlatformLayer.h`
- `core/platform/src/PlatformLayer.cpp`

This module provides additive platform contracts:

- `PermissionPolicyStore`
- `ExecutionMemoryStore`
- `TemporalStateEngine`
- `IntentSequenceExecutor`
- `WorkflowExecutor`
- `SemanticPlannerBridge`
- UCP envelope serializers

## Pillar Mapping (Phase 11)

1. Self-healing execution
- `SelfHealingExecutor` in `core/action` provides bounded, deterministic recovery.
- Recovery sequence is fixed: `retry` -> `alternate_node` -> `fallback_reveal`.

2. Temporal unified state
- `TemporalStateEngine` records unified state snapshots and transitions.
- Adds stability and frame consistency computations.

3. Multi-step execution
- `IntentSequenceExecutor` runs deterministic step plans.
- API: `POST /act/sequence`.

4. Semantic interface
- `SemanticPlannerBridge` parses semantic goals and emits deterministic plan envelopes.
- API: `POST /task/semantic`.

5. Experience memory
- `ExecutionMemoryStore` records per-node success/failure/fallback/latency.
- Target resolution includes execution-memory success bias.

6. Adapter ecosystem
- `AdapterMetadata` contract and `AdapterRegistry::ListMetadata()`.
- API: `GET /adapters`.

7. Hybrid perception
- `EnvironmentPerception` now carries lightweight text/grouping/region labels.

8. Policy layer
- Global policy store + checks:
  - `GET /policy`
  - `POST /policy`
- Enforcement is integrated in `ActionExecutor` and `/execute` API path.

9. Workflow orchestration
- `WorkflowExecutor` wraps deterministic sequence execution behavior.
- API: `POST /workflow/run`.

10. UCP protocol
- Added protocol envelopes:
  - `POST /ucp/act`
  - `GET /ucp/state`

11. Performance + scale metrics
- `LatencyPercentilesSnapshot` with `p50/p95/p99/p999`.
- API: `GET /perf/percentiles`.
- Frame consistency API: `GET /perf/frame-consistency`.

12. Demonstration surfaces
- API now exposes full phase contracts for semantic planning, policy gating, sequence/workflow execution, and UCP envelopes.

## Updated Data Flow

```text
Observer -> Intent Registry -> InteractionGraph/TaskPlanner -> Action Interface
                                                        |            |
                                                        |            v
                                                        |      Execution Contract
                                                        |            |
Environment Adapter -> Unified EnvironmentState --------+------------+----> ExecutionEngine -> Adapters
        |                                                                |
        +--> LightweightPerception + ScreenState                           +--> AdapterMetadata
        |
        +--> TemporalStateEngine -> State history / transition / consistency

Platform Layer
  - policy checks
  - execution memory
  - semantic bridge
  - sequence/workflow
  - UCP serialization

Telemetry
  - traces
  - latency + performance contract
  - percentile snapshots
  - persistence
```

## API Surface (v2.0)

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

## Compatibility and Determinism

- v1.x routes and contracts remain intact.
- Runtime behavior remains bounded:
  - fixed candidate limits
  - deterministic tie-breaks
  - bounded retry/recovery attempts
- No heavy model dependency is introduced into core runtime.

## Validation Baseline

- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Release`
- Test: `ctest --test-dir build -C Release --output-on-failure`
