# IEE Context Handoff (v1.4)

## 1. What Are We Building
The Intent Execution Engine (IEE): a deterministic native execution layer that converts live OS/application state into a structured intent space and executes actions through verifiable adapters. v1.4 extends v1.3 into a closed-loop decision/feedback/prediction runtime while preserving deterministic control semantics.

## 2. Current State
Completed in v1.4:
- Environment abstraction primitives:
  - `EnvironmentAdapter`
  - `EnvironmentState`
  - `RegistryEnvironmentAdapter`
  - `MockEnvironmentAdapter`
- High-frequency observation system:
  - `ObservationPipeline` with double-buffered state handoff
  - pipeline metrics for sampling health and latency
- Lightweight perception layer:
  - dominant surface classification
  - focus/occupancy ratios
  - UI signature
  - region density/focus map
- Macro composition and execution:
  - `ActionSequence`
  - DSL parsing for stream controls
  - `MacroExecutor`
- Dual-pipeline runtime synchronization:
  - control runtime now executes against latest observation snapshot context
- Latency profiling:
  - telemetry phase breakdown (observation/perception/queue/execution/verification/total)
  - CLI command `iee latency`
- API streaming surface:
  - `GET /stream/state`
  - `POST /stream/control`
  - `POST /control/start` supports observation interval tuning
- Decision + prediction contracts:
  - `DecisionProvider`
  - `Predictor`
  - `FeedbackDelta` helpers
- Closed-loop runtime integration:
  - non-blocking decision worker with bounded budget
  - feedback capture before/after each execution
  - mismatch tracking and bounded correction enqueue
- Macro v2:
  - `loop` and `if_visible` branching support
- Input timing precision:
  - `delay_ms`, `hold_ms`, `sequence_ms`
- New API routes:
  - `POST /predict`
  - `GET /perf`
  - `GET /stream/live` (SSE push)
- New CLI route:
  - `iee perf`
- Validation:
  - `cmake --build build --config Debug` passes
  - `ctest --test-dir build -C Debug --output-on-failure` passes (`12/12`)

Partially built / open for hardening:
- Stream payload schema is still intentionally flat and string-valued.
- Macro execution is deterministic but non-transactional.
- SSE is now available but connection-bounded (not yet centralized push broker).
- VS Code CMake Tools helper path still fails to configure in this environment.

## 3. Last Work Done
- Added `DecisionInterfaces` with pluggable decision and prediction contracts.
- Integrated `ControlRuntime` decision worker and feedback/correction loop.
- Added telemetry performance-contract computation + serialization.
- Added `iee perf` CLI command.
- Added `/predict`, `/perf`, and `/stream/live` endpoints.
- Upgraded macro parser/executor for Macro v2 (`loop`, `if_visible`, else-branch).
- Added input timing precision path in `InputAdapter`.
- Added/updated tests:
  - new `integration_closed_loop_feedback`
  - expanded `integration_api_hardening` with v1.4 endpoint and macro-v2 coverage

## 4. Current Problem
No active blocker in the validated v1.4 scope.

Known non-blocking issues:
1. CMake Tools VS Code helpers remain unavailable.
2. Stream payload parser is intentionally strict and flat.
3. Macro execution has no rollback semantics.

## 5. Next Plan
1. Add typed/nested JSON schema mode for stream-control and prediction payloads while preserving current flat compatibility.
2. Add optional transactional macro mode (compensation hooks or dry-run verification pass).
3. Extend SSE from bounded sessions to long-lived brokered fan-out.
4. Add long-duration soak tests for feedback correction stability and jitter budgets.
5. Resolve VS Code CMake Tools integration gap for native build/test workflows.

## 6. Key Decisions Taken
- Keep v1.3 public/runtime behavior intact and deliver v1.4 as additive architecture.
- Treat environment state as a first-class runtime primitive, not ad-hoc API payload assembly.
- Keep perception lightweight and deterministic (no heavy CV/ML dependencies).
- Separate observation and execution into synchronized pipelines for real-time safety.
- Expose phase-level latency telemetry and contract metrics to make performance tuning evidence-driven.
- Keep decision/prediction as optional plugin contracts; no embedded AI stack in core runtime.

## Multi-Agent Protocol Record
Agents used in this v1.4 cycle:
- Architecture agent: defined environment-layer and dual-pipeline boundaries.
- Core implementation agent: implemented runtime decision/feedback/prediction, API/CLI, and test updates.
- Debugging agent: diagnosed compile/type integration issues and endpoint behavior edge cases.
- Refactoring agent: enforced modular plugin contracts and bounded runtime behavior.
- Documentation agent: synchronized architecture/status/parity/issues/context docs with validated v1.4 state.
