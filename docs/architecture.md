# IEE v1.5 Architecture

## Purpose
IEE v1.5 extends v1.4 with deterministic screen perception and a unified screen state model. The new layer merges UIA state, lightweight visual detections, and cursor context without introducing heavyweight ML dependencies.

## Runtime Modules

### core/observer
- Captures active window/process/cursor context and filesystem snapshots.
- Continues to provide deterministic sequence IDs consumed by execution and telemetry paths.

### core/execution Environment + Observation Layer
- `EnvironmentState` now carries:
  - `screenFrame` (capture metadata)
  - `screenState` (unified screen model)
  - `visionTiming` (capture/detection/merge timings)
- `ObservationPipeline` remains high-frequency and double-buffered, and now tracks vision-specific metrics:
  - latest frame id
  - capture/detection/merge latency summaries
  - estimated observation FPS

### core/execution Screen Perception (new in v1.5)
- `ScreenCaptureEngine`
  - DXGI Desktop Duplication first
  - deterministic fallback to screen-metrics capture metadata when duplication is unavailable
- `VisualDetector`
  - lightweight heuristics over UI bounds and spatial occupancy
  - edge-density proxy, region segmentation, color-cluster bucketing, text-like cues
- `ScreenStateAssembler`
  - merges UI and visual candidates
  - deduplicates by overlap
  - emits stable element ids and unified signatures
  - always includes cursor as a first-class screen element

### core/execution Control Runtime (v1.5 upgrade)
- Retains v1.4 dual-lane execution model and decision/feedback contracts.
- Adds observation vision stats to runtime status payload:
  - latest frame id
  - last/average capture/detection/merge timings
  - estimated observation FPS
- Logs per-frame vision timing samples into telemetry for API/CLI observability.

### core/telemetry (v1.5 upgrade)
- Adds dedicated vision-latency sample stream:
  - capture, detection, merge, total
  - frame id + environment sequence + timestamp
  - simulated-frame and dropped-frame accounting
- Adds vision aggregates and JSON serialization (`SerializeVisionJson`).

### interface/api (v1.5 upgrade)
- New endpoint: `GET /stream/frame`
  - `mode=full` for complete `ScreenState`
  - `mode=delta` with optional `since=<frame_id>` for incremental updates
  - bounded frame history and reset signaling when delta base is unavailable
- `GET /stream/state` now always includes unified screen state + vision timing.

### interface/cli (v1.5 upgrade)
- New command: `iee vision`
  - table and JSON views for capture/detect/merge/total latency
  - dropped-frame + simulated-frame + FPS visibility

## Core Flow (v1.5)
1. Capture synchronized environment state through observation pipeline.
2. Acquire screen frame metadata through DXGI duplication (or deterministic fallback).
3. Run lightweight visual detection primitives.
4. Merge UIA + visual + cursor into unified `ScreenState` with stable ids.
5. Submit latest state to optional decision worker (non-blocking to frame loop).
6. Queue and execute intents under frame latency budget.
7. Capture feedback deltas and schedule bounded corrections when applicable.
8. Record telemetry traces, phase latency breakdowns, and vision latency aggregates.
9. Serve deterministic control and observability via CLI and HTTP API (`/stream/state`, `/stream/frame`, `/stream/live`, `/perf`).

## Validation Baseline
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`

Latest verified result: `14/14` tests passing.
