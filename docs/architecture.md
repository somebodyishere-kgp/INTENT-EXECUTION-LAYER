# IEE v1.3 Architecture

## Purpose
IEE v1.3 evolves the runtime from control-loop execution into environment-aware real-time control. The system now maintains synchronized high-frequency environment state, computes lightweight perception primitives, composes macro actions, and exposes stream-focused state/control APIs while preserving v1.2 compatibility.

## Runtime Modules

### core/observer
- Captures active window/process/cursor context and filesystem snapshots.
- Supplies snapshot sequence IDs that continue to anchor deterministic intent context.

### core/execution Environment Layer (new in v1.3)
- `EnvironmentAdapter` abstraction defines environment capture contract.
- `EnvironmentState` is the canonical runtime environment frame:
  - active process/window
  - cursor
  - UI and filesystem surfaces
  - perception summary
  - source snapshot provenance
- `RegistryEnvironmentAdapter` bridges live `IntentRegistry` state into `EnvironmentState`.
- `MockEnvironmentAdapter` provides deterministic simulation frames for tests and stress harnesses.

### core/execution Observation Pipeline (new in v1.3)
- `ObservationPipeline` runs independent high-frequency sampling in a dedicated thread.
- Uses double-buffered snapshots for low-contention producer/consumer exchange.
- Exposes metrics: running state, sample count, failures, latest sequence, capture timing.

### core/execution Lightweight Perception (new in v1.3)
- `LightweightPerception` computes non-ML primitives from current UI structure:
  - dominant interaction surface classification (`form`, `command`, `navigation`, `list`, `unknown`)
  - focus ratio
  - occupancy ratio
  - deterministic UI signature
  - active regions (3x3 grid density + focus)
- No CV/ML dependency is introduced.

### core/execution Macro Composition (new in v1.3)
- `ActionSequence` + `ActionStep` model reusable multi-step actions.
- `MacroExecutor` executes ordered sequences against synchronized environment snapshots.
- Supports repeated single-action expansion and compact DSL-defined macros.

### core/execution Control Runtime (v1.3 upgrade)
- `ControlRuntime` now operates as dual synchronized pipelines:
  - observation pipeline: high-frequency state capture
  - execution pipeline: priority queues + budgeted dispatch
- Runtime snapshots include observation telemetry:
  - adapter
  - latest observation sequence
  - sample count
  - last capture latency
- Enqueue timestamps are retained to compute queue-wait latency breakdown.

### core/telemetry (v1.3 upgrade)
- Existing trace persistence and adapter metrics remain intact.
- Added latency breakdown model:
  - observation
  - perception
  - queue wait
  - execution
  - verification
  - total
- Supports aggregate stats (`avg`, `p95`, `max`) and latest-sample serialization.

### interface/api (v1.3 upgrade)
- Existing control/execute/explain endpoints retained.
- Added streaming endpoints:
  - `GET /stream/state` returns synchronized environment state + perception + latency aggregate
  - `POST /stream/control` executes or queues single/macro control sequences
- `POST /control/start` accepts observation interval tuning.

### interface/cli (v1.3 upgrade)
- Existing operator commands retained.
- Added latency profiler command:
  - `iee latency`
  - `iee latency --json`

## Core Flow (v1.3)
1. Observe source system state through observer + event updates.
2. Transform observer snapshots into canonical `EnvironmentState`.
3. Sample environment continuously via `ObservationPipeline` (double-buffered).
4. Compute lightweight perception primitives on each environment capture.
5. Resolve/queue intents in control runtime execution lane.
6. Synchronize execution context with latest environment snapshot.
7. Execute single intent or macro sequence with budget constraints.
8. Record trace telemetry + latency breakdown + persistence events.
9. Serve stream/control/readout through CLI and HTTP API.

## Validation Baseline
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`

Latest verified result: `11/11` tests passing.
