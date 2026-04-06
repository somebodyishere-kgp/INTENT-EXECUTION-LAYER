# IEE v1.4 Status

## Date
2026-04-06

## Current State
IEE has been upgraded to v1.4 decision + feedback + prediction layer. The runtime now supports optional bounded decision-provider integration, prediction hooks, closed-loop feedback deltas with correction scheduling, push streaming transport, performance contracts, and Macro v2 flow control while preserving v1.3 behavior.

## Completed v1.4 Work
- Environment abstraction layer:
  - added `EnvironmentAdapter` contract
  - added canonical `EnvironmentState`
  - added live `RegistryEnvironmentAdapter`
  - added simulation `MockEnvironmentAdapter`
- High-frequency observation pipeline:
  - added `ObservationPipeline` with dedicated sampling thread
  - implemented double-buffered state handoff
  - added pipeline metrics (samples/failures/latest sequence/capture timings)
- Lightweight perception primitives:
  - added dominant-surface classification
  - added focus ratio and occupancy ratio
  - added deterministic UI signature
  - added region density/focus extraction
- Macro/action composition:
  - added `ActionSequence` and `ActionStep`
  - added `MacroExecutor` for ordered multi-step execution
  - added compact DSL parsing for stream-control macros
- Dual pipeline synchronization:
  - control runtime now consumes synchronized observation snapshots
  - execution context receives observation-linked snapshot/version/cursor/window data
- Latency breakdown profiling:
  - telemetry now records observation/perception/queue/execution/verification/total timing phases
  - added aggregate summary (`avg`, `p95`, `max`) and latest sample output
  - added CLI command `iee latency`
- Streaming API surface:
  - added `GET /stream/state`
  - added `POST /stream/control` (immediate or queued mode)
  - added `observationIntervalMs` support to `POST /control/start`
- Decision + prediction interfaces:
  - added `DecisionInterfaces` contracts (`DecisionProvider`, `Predictor`, feedback helpers)
  - integrated optional, non-blocking decision worker into `ControlRuntime`
  - added runtime predictor hook plumbing
- Closed-loop feedback:
  - added before/after state capture around execution
  - added feedback delta computation and mismatch detection
  - added bounded correction enqueue path
  - added runtime feedback/decision counters in control status
- Input timing precision:
  - added timing parameter parsing (`delay_ms`, `hold_ms`, `sequence_ms`)
  - added hold-aware key/mouse execution behavior in input adapter
- Macro v2:
  - added `loop` and `if_visible` constructs in action-sequence DSL
  - added optional else branch support and repeat handling
- Push and prediction API surface:
  - added `POST /predict`
  - added `GET /stream/live` (SSE)
  - added `GET /perf`
- Performance contract exposure:
  - added telemetry-side contract metrics (`p50`, `p95`, `max`, jitter, drift, within-budget)
  - added CLI command `iee perf`
- Simulation/testing updates:
  - added `integration_closed_loop_feedback`
  - extended `integration_api_hardening` with `/predict`, `/perf`, `/stream/live`, and Macro v2 flow coverage

## Verification Executed
- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Debug`
- Automated tests:
  - `ctest --test-dir build -C Debug --output-on-failure`
  - Result: `12/12` passing tests

## Remaining Non-Blocking Gaps
- Stream control JSON parser remains intentionally flat/string-valued for transport hardening.
- Macro execution is deterministic but currently non-transactional (partial success can occur in longer sequences).
- SSE endpoint is intentionally bounded per connection (event-count capped) and not yet a long-lived brokered channel.
- VS Code CMake Tools build/test helper integration remains unavailable in this environment.

## Tooling Note
- `Build_CMakeTools` and `RunCtest_CMakeTools` failed to configure in this session with empty diagnostics.
- Authoritative verification was completed through direct `cmake` + `ctest` execution.
