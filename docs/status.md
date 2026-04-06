# IEE v1.3 Status

## Date
2026-04-06

## Current State
IEE has been upgraded to v1.3 environment-aware real-time control. The runtime now supports environment abstraction, high-frequency double-buffered observation, lightweight perception, macro action composition, streaming state/control APIs, and latency phase profiling, while preserving v1.2 behavior.

## Completed v1.3 Work
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
- Simulation/testing updates:
  - added `unit_observation_pipeline`
  - extended `integration_api_hardening` with stream state/control and macro coverage

## Verification Executed
- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Debug`
- Automated tests:
  - `ctest --test-dir build -C Debug --output-on-failure`
  - Result: `11/11` passing tests

## Remaining Non-Blocking Gaps
- Stream control JSON parser remains intentionally flat/string-valued for transport hardening.
- Macro execution is deterministic but currently non-transactional (partial success can occur in longer sequences).
- Streaming is request/response polling style (SSE/WebSocket not yet introduced).
- VS Code CMake Tools build/test helper integration remains unavailable in this environment.

## Tooling Note
- `Build_CMakeTools` and `RunCtest_CMakeTools` failed to configure in this session with empty diagnostics.
- Authoritative verification was completed through direct `cmake` + `ctest` execution.
