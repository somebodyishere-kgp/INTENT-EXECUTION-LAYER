# IEE v1.5 Requirement Parity

## Objective Coverage

| Phase / Requirement | Expected | Implemented | Status |
|---|---|---|---|
| v1.5 Phase 1: Screen capture engine | DXGI/Desktop Duplication capture path, low-latency frame metadata | Added `ScreenCaptureEngine` with DXGI duplication attempt and deterministic fallback capture path | Met |
| v1.5 Phase 2: Lightweight visual detection | Edge/segmentation/color/text-like heuristics without heavy ML | Added `VisualDetector` heuristics with segmentation, edge-density proxy, color cluster, text-like tagging | Met |
| v1.5 Phase 3: Unified screen model | Merge UIA + visual + cursor with dedup and stable ids | Added `ScreenStateAssembler`, unified `ScreenState`, overlap-based merge, stable IDs, cursor element | Met |
| v1.5 Phase 4: Observation/control integration | Unified screen state available in runtime observation loop | Extended `EnvironmentState`, `ObservationPipeline`, and `ControlRuntime` status with screen and vision metrics | Met |
| v1.5 Phase 5: Frame streaming endpoint | `GET /stream/frame` full + delta updates | Added `/stream/frame` with `mode=full|delta`, optional `since`, bounded frame history, reset signaling | Met |
| v1.5 Phase 6: Vision performance metrics | Capture/detect/merge latency and FPS visibility | Added telemetry `VisionLatencySample` + aggregate JSON; observation/control status now includes vision timing + FPS | Met |
| v1.5 Phase 7: CLI visibility | `iee vision` command | Added `iee vision` command with table and JSON output | Met |
| v1.5 Phase 8: Validation suite | Capture/detect/merge + stress + dynamic scenarios | Added `unit_screen_perception`, `stress_screen_pipeline`, and `/stream/frame` API integration checks | Met |
| Compatibility constraint | Preserve v1.4 behavior | Existing v1.4 routes and control loop semantics retained; previous tests remain green | Met |
| Minimalism constraint | No heavy CV/ML runtime | Pure deterministic heuristics; no heavyweight model/runtime dependency added | Met |

## Verified Runtime Behaviors
- `GET /stream/state` now includes unified screen state and vision timing.
- `GET /stream/frame` returns full frame payloads and bounded delta payloads.
- Control runtime status reports vision latency and observation FPS.
- Telemetry now reports vision capture/detection/merge/total aggregates.
- CLI now exposes vision telemetry through `iee vision`.
- Prior v1.4 decision/feedback/predict/perf/macro behaviors remain active.

## Validation Snapshot
- Build: successful (`Debug`)
- Tests: `14/14` passing (`ctest --test-dir build -C Debug --output-on-failure`)

## Residual Gaps (Non-Blocking)
- Visual detection remains heuristic (text-like cues only, no OCR/classification model).
- Frame delta history is intentionally bounded and can require client reset on stale cursors.
- Stream payload parser still intentionally accepts flat string-valued JSON fields.
- Macro execution remains non-transactional for partial failures.
- VS Code CMake Tools helper integration remains unavailable in this environment.
