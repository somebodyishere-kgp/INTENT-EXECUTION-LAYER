# IEE v1.7 Status

## Date
2026-04-08

## Current State
IEE has been upgraded to v1.7 with an additive AI-facing interface layer over the v1.6 production execution graph.

The runtime now supports:

- stateless in-process SDK access (`IEEClient`)
- deterministic planning-only task decomposition (`TaskPlanner`)
- reveal-aware execution guarantees (`ExecutionContract` + `RevealExecutor`)
- compact model-facing state projection (`AIStateView`)
- strict latency contract reporting in both API and CLI surfaces

## Completed v1.7 Work
- SDK layer:
  - added `interface/sdk/include/IEEClient.h`
  - added `GetState`, `GetStateAiJson`, and contract-backed `Execute`
- Task interface:
  - added `TaskRequest`, `TaskPlanCandidate`, `TaskPlanResult`, and `TaskPlanner`
  - added planning-only endpoint `POST /task/plan`
- Reveal and execution guarantees:
  - added `RevealExecutor` with bounded retries and reveal verification checks
  - added `ExecutionContract` to enforce `reveal -> execute -> verify`
  - upgraded `/execute` responses with contract/reveal diagnostics
- AI projection:
  - added `AIStateView` + projector
  - added `GET /state/ai`
- Latency strict mode:
  - upgraded `/perf` with strict status fields and strict conflict behavior
  - upgraded CLI `iee perf --strict`
- Developer experience:
  - added CLI demos: `iee demo presentation|browser [--json] [--run]`
  - added `docs/ai_sdk.md`
- Validation:
  - added `scenario_task_interface`
  - extended `integration_api_hardening` for `/state/ai`, `/task/plan`, and strict perf

## Retained Prior Behavior
- v1.5.1 and v1.6 routes remain available.
- UIG stable IDs, descriptor/state split, reveal metadata, and graph delta contracts remain additive-compatible.
- Telemetry persistence, control runtime, and stream endpoints remain intact.

## Verification
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`

## Remaining Non-Blocking Gaps
- Reveal steps currently map to generic activation primitives; adapter-specific reveal specializations can be expanded.
- Task planning is deterministic keyword/domain based; deeper semantic ranking remains future work.
- Graph history for API delta remains intentionally bounded for memory safety.
