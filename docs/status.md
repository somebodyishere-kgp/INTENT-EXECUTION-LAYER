# IEE v1.5 Status

## Date
2026-04-06

## Current State
IEE has been upgraded to v1.5 screen perception + unified screen state. The runtime now fuses UIA observations with lightweight visual detections and cursor context, exposes frame-level streaming with delta support, and adds dedicated vision latency telemetry while preserving v1.4 behavior.

## Completed v1.5 Work
- Screen capture engine:
  - added `ScreenCaptureEngine` with DXGI Desktop Duplication attempt path
  - added deterministic fallback when duplication is unavailable
  - unified frame metadata (`frameId`, dimensions, simulated/valid flags)
- Lightweight visual detection:
  - added `VisualDetector` with deterministic heuristics
  - includes region segmentation, edge-density proxy, color clustering, text-like hints
- Unified `ScreenState` model:
  - added `ScreenElement` and `VisualElement` contracts
  - merged UIA + visual candidates with overlap-based dedup
  - stable element id generation and unified screen signature
  - cursor promoted to explicit screen element in merged state
- Observation pipeline + runtime integration:
  - `EnvironmentState` now carries `screenFrame`, `screenState`, `visionTiming`
  - observation metrics now include latest frame id, capture/detect/merge timings, estimated FPS
  - control runtime status surfaces vision timing summaries
- Frame streaming API:
  - added `GET /stream/frame`
  - supports `mode=full`
  - supports `mode=delta` with optional `since=<frame_id>`
  - includes reset signaling when requested base frame is unavailable
- Performance and telemetry:
  - added telemetry-side `VisionLatencySample` logging
  - added vision aggregate snapshot + serialization (`SerializeVisionJson`)
  - includes dropped-frame and simulated-frame counters
- CLI visibility:
  - added `iee vision` command for human-readable and JSON vision metrics
- Validation and test expansion:
  - added `unit_screen_perception`
  - added `stress_screen_pipeline`
  - extended `integration_api_hardening` with `/stream/frame` coverage

## Verification Executed
- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Debug`
- Automated tests:
  - `ctest --test-dir build -C Debug --output-on-failure`
  - Result: `14/14` passing tests

## Remaining Non-Blocking Gaps
- Visual pipeline intentionally avoids heavy OCR/object models; text-like detection is heuristic only.
- Frame delta history is intentionally bounded; stale `since` pointers can trigger `reset_required`.
- Stream control JSON parser remains intentionally flat/string-valued for transport hardening.
- Macro execution remains deterministic but non-transactional for longer sequences.
- VS Code CMake Tools build/test helper integration remains unavailable in this environment.

## Tooling Note
- `Build_CMakeTools` and `RunCtest_CMakeTools` failed to configure in this session with empty diagnostics.
- Authoritative verification was completed through direct `cmake` + `ctest` execution.
