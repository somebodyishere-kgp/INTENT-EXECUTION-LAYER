# Issues and Errors Log (v1.5 Upgrade)

## Date
2026-04-06

## Resolved During v1.5 Migration

### 1. CMake Tools configure/build orchestration failure
- Symptom: `Build_CMakeTools` / `RunCtest_CMakeTools` failed with "Unable to configure the project" and no diagnostics.
- Root cause: environment-specific VS Code CMake Tools integration fault.
- Fix: switched to direct `cmake` + `ctest` command path for authoritative verification.
- Result: full v1.5 build and test validation completed successfully.

### 2. RECT LONG/int template ambiguity in screen perception
- Symptom: MSVC template resolution failures for `std::max`/`std::clamp` in `ScreenPerception.cpp`.
- Root cause: mixed `LONG` (RECT/POINT fields) and `int` arguments in templated comparisons.
- Fix: explicit casts and typed clamp/max bounds in rect/cursor normalization helpers.
- Result: new screen perception module compiles cleanly.

### 3. Stress threshold instability across host performance profiles
- Symptom: initial `stress_screen_pipeline` threshold failed intermittently on slower hosts.
- Root cause: threshold assumed fixed sample volume independent of host timing variance.
- Fix: aligned stress assertions with explicit 30+ FPS target and sustained sampling guarantees.
- Result: stress test now validates intended contract without false negatives.

### 4. Frame-stream delta baseline availability
- Symptom: delta clients can request stale `since` frame ids outside bounded history.
- Root cause: `/stream/frame` intentionally uses a bounded in-memory history.
- Fix: endpoint emits `reset_required=true` and includes full state when base frame is unavailable.
- Result: deterministic client recovery path added.

## Current Known Risks (Non-Blocking)

### 1. Flat stream payload schema
- Current behavior: stream-control parser accepts flat string-valued JSON objects only.
- Risk: typed/nested payload clients require normalization layer.

### 2. Heuristic visual detection limits
- Current behavior: visual perception remains lightweight and deterministic (no OCR/object models).
- Risk: semantic richness of visual understanding is intentionally limited.

### 3. Macro partial completion semantics
- Current behavior: sequence execution is deterministic and stop-on-failure capable but non-transactional.
- Risk: long macros may leave intermediate side effects on failed step.

### 4. Bounded frame-delta history model
- Current behavior: frame delta uses bounded in-memory history.
- Risk: long-disconnected clients can require state reset/re-sync.

### 5. Bounded SSE stream session model
- Current behavior: SSE connection emits bounded events per request (`events` cap).
- Risk: long-lived fan-out stream scenarios still require brokered streaming infrastructure.

### 6. IDE tooling parity
- Current behavior: VS Code CMake Tools helper path remains unavailable.
- Risk: build/test ergonomics in IDE remain degraded despite green command-line verification.

## Environment Notes
- Command-line verification remains the authoritative path in this environment.
- Latest validation state: build success + `14/14` test pass.
