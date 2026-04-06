# IEE v1.4 Architecture

## Purpose
IEE v1.4 evolves v1.3 into a deterministic closed-loop runtime. The system now adds optional decision and prediction providers, feedback-delta analysis, correction scheduling, push streaming transport, performance contracts, and Macro v2 control flow while preserving v1.3 compatibility.

## Runtime Modules

### core/observer
- Captures active window/process/cursor context and filesystem snapshots.
- Provides deterministic sequence IDs used across execution, feedback, and prediction surfaces.

### core/execution Environment + Observation Layer
- `EnvironmentAdapter`, `EnvironmentState`, `RegistryEnvironmentAdapter`, and `MockEnvironmentAdapter` remain the canonical environment surface.
- `ObservationPipeline` remains high-frequency and double-buffered.

### core/execution Control Runtime (v1.4 upgrade)
- Continues dual synchronized lanes:
  - observation lane: capture + perception
  - execution lane: queued intents under bounded frame budget
- New optional intelligence hooks:
  - `DecisionProvider` integration through a dedicated decision worker thread
  - bounded decision budget (`decisionBudgetMs`) to keep control loop non-blocking
- New closed-loop feedback path:
  - captures before/after state per execution
  - computes `FeedbackDelta`
  - tracks mismatch counters
  - schedules one bounded correction retry for eligible mismatches
- New runtime surfaces:
  - decision counters (produced, timeout)
  - feedback counters (samples, mismatches, corrections)

### core/execution Decision Interfaces (new in v1.4)
- `DecisionProvider` abstraction for external deterministic decision engines.
- `Predictor` abstraction for state prediction hooks.
- Shared feedback utilities:
  - `ComputeFeedbackDelta(...)`
  - `IsTargetVisible(...)`

### core/execution Macro Composition (v1.4 upgrade)
- Existing action sequence model retained.
- Macro v2 DSL now supports:
  - `loop|<count>|<action:args>`
  - `if_visible|<target>|<then_action:args>|<else_action:args>`
- Step-level options supported for timing/required/repeat behavior.

### core/execution Input Adapter (v1.4 upgrade)
- Added `TimedIntent` parsing path.
- Supports millisecond precision parameters:
  - `delay_ms`
  - `hold_ms`
  - `sequence_ms`
- Applies deterministic hold timing for key/mouse operations.

### core/telemetry (v1.4 upgrade)
- Retains trace persistence and latency breakdown tracking.
- Adds performance contract model:
  - `p50`, `p95`, `max`
  - jitter and drift
  - budget compliance boolean

### interface/api (v1.4 upgrade)
- Existing endpoints retained.
- Added:
  - `POST /predict`
  - `GET /perf`
  - `GET /stream/live` (SSE push stream)
- `POST /control/start` now accepts `decisionBudgetMs`.

### interface/cli (v1.4 upgrade)
- Existing commands retained.
- Added `iee perf` and JSON output mode for performance contract visibility.

## Core Flow (v1.4)
1. Capture synchronized environment state through observation pipeline.
2. Compute lightweight perception primitives.
3. Submit latest frame to optional decision worker (non-blocking to control frame loop).
4. Queue and execute intents under frame latency budget.
5. Capture post-execution state and compute feedback delta.
6. Detect mismatch and schedule bounded correction when applicable.
7. Record telemetry traces, phase latency breakdowns, and performance-contract aggregates.
8. Serve deterministic control and observability via CLI and HTTP API (including SSE push stream).

## Validation Baseline
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`

Latest verified result: `12/12` tests passing.
