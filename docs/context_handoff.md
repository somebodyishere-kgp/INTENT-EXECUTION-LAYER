# IEE Context Handoff (v1.5)

## 1. What Are We Building
The Intent Execution Engine (IEE): a deterministic native execution layer that converts live OS/application state into a structured intent space and executes actions through verifiable adapters. v1.5 extends v1.4 by adding screen perception and a unified screen state surface while preserving the existing decision/feedback/prediction runtime.

## 2. Current State
Completed in v1.5:
- Screen perception primitives:
  - `ScreenCaptureEngine` (DXGI Desktop Duplication attempt + fallback)
  - `VisualDetector` (lightweight deterministic heuristics)
  - `ScreenStateAssembler` (UIA + visual + cursor merge with stable IDs)
- Environment model upgrade:
  - `EnvironmentState` now carries `screenFrame`, `screenState`, and `visionTiming`
- Observation pipeline upgrade:
  - metrics now include latest frame id, vision capture/detect/merge latencies, and estimated FPS
- Control runtime upgrade:
  - control status now reports vision timing summaries and latest frame telemetry
  - runtime logs per-observation vision samples into telemetry
- Telemetry upgrade:
  - added `VisionLatencySample` stream and aggregate `VisionSnapshot`
  - added `SerializeVisionJson(...)`
- API upgrade:
  - added `GET /stream/frame` with full + delta modes
  - bounded frame-history delta computation with `reset_required` fallback path
  - `GET /stream/state` now returns unified screen state + vision timing
- CLI upgrade:
  - added `iee vision` command (table + JSON)
- Test upgrade:
  - added `unit_screen_perception`
  - added `stress_screen_pipeline`
  - extended `integration_api_hardening` for `/stream/frame`
- Validation:
  - `cmake --build build --config Debug` passes
  - `ctest --test-dir build -C Debug --output-on-failure` passes (`14/14`)

Open for hardening (non-blocking):
- Visual detection remains heuristic-only (no OCR/semantic model).
- Frame delta history is bounded and can require reset on stale cursors.
- Stream-control payload schema remains intentionally flat/string-valued.
- Macro execution remains deterministic but non-transactional.
- VS Code CMake Tools helper path still fails to configure in this environment.

## 3. Last Work Done
- Added `core/execution/include/ScreenPerception.h` + `core/execution/src/ScreenPerception.cpp`.
- Extended environment and observation contracts for unified screen state.
- Added telemetry vision aggregation and JSON serialization.
- Added `/stream/frame` API route with delta support.
- Added `iee vision` CLI command.
- Added v1.5 tests and updated CMake test registrations.
- Synchronized docs: architecture/status/parity/issues/context + README + repository about metadata text.

## 4. Current Problem
No active blocker in the validated v1.5 scope.

Known non-blocking issues:
1. VS Code CMake Tools helpers remain unavailable in this environment.
2. Visual detection is intentionally lightweight and heuristic.
3. Frame delta history is bounded and reset-driven for stale cursors.

## 5. Next Plan
1. Add optional richer visual analyzers (still deterministic and bounded) for stronger text/semantic cues.
2. Add explicit client cursor/token contract for long-lived frame-delta synchronization.
3. Add long-duration soak tests for frame-stream delta stability under bursty UI changes.
4. Add typed/nested payload mode for stream-control while preserving flat compatibility.
5. Resolve VS Code CMake Tools integration gap for native build/test workflows.

## 6. Key Decisions Taken
- Keep v1.4 public/runtime behavior intact and deliver v1.5 as additive architecture.
- Keep perception deterministic and lightweight; no heavy ML/CV runtime dependencies.
- Treat unified `ScreenState` as a first-class runtime artifact, not just a transport payload.
- Keep frame streaming deterministic with bounded history and explicit reset behavior.
- Expose vision latency as a first-class telemetry channel and CLI/API surface.

## Multi-Agent Protocol Record
Agents used in this v1.5 cycle:
- Architecture agent: defined screen perception boundaries and merge contracts.
- Core implementation agent: integrated capture/detect/merge into runtime/API/CLI.
- Debugging agent: identified compile and host-performance threshold issues.
- Refactoring agent: validated modular boundaries and bounded history design.
- Documentation agent: synchronized architecture/status/parity/issues/context/README artifacts.
