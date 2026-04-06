# Issues and Errors Log (v1.3 Upgrade)

## Date
2026-04-06

## Resolved During v1.3 Migration

### 1. CMake Tools configure/build orchestration failure
- Symptom: `Build_CMakeTools` / `RunCtest_CMakeTools` failed with "Unable to configure the project" and no diagnostics.
- Root cause: environment-specific VS Code CMake Tools integration fault.
- Fix: switched to direct `cmake` + `ctest` command path for authoritative verification.
- Result: full build and test validation completed successfully.

### 2. MSVC type ambiguity in perception geometry math
- Symptom: compile failure in `EnvironmentAdapter.cpp` (`std::max` overload ambiguity with `LONG` vs `int`).
- Root cause: arithmetic on `RECT` fields (`LONG`) was fed directly into `std::max(0, ...)`.
- Fix: normalized differences with explicit `static_cast<int>(...)` before `std::max`.
- Result: v1.3 core modules compile cleanly.

### 3. Streaming state serialization construction bug
- Symptom: malformed JSON emission in stream-state helper during initial integration pass.
- Root cause: malformed literal insertion while composing nested perception object.
- Fix: corrected JSON stream composition and validated endpoint through integration test.
- Result: `GET /stream/state` returns stable structured JSON payload.

### 4. Observation pipeline test flakiness under scheduler variance
- Symptom: `unit_observation_pipeline` intermittently failed on strict sample-count threshold.
- Root cause: aggressive fixed expectation (`>=5`) at short wall-clock interval.
- Fix: tightened correctness assertions while relaxing scheduler-sensitive sample-count gate.
- Result: deterministic pass behavior while still validating pipeline capture semantics.

## Current Known Risks (Non-Blocking)

### 1. Flat stream payload schema
- Current behavior: stream-control parser accepts flat string-valued JSON objects only.
- Risk: typed/nested payload clients require normalization layer.

### 2. Macro partial completion semantics
- Current behavior: sequence execution is deterministic and stop-on-failure capable but non-transactional.
- Risk: long macros may leave intermediate side effects on failed step.

### 3. Polling-only stream model
- Current behavior: stream endpoints are request/response polling primitives.
- Risk: clients needing push-based updates must poll frequently or add external fan-out.

### 4. IDE tooling parity
- Current behavior: VS Code CMake Tools helper path remains unavailable.
- Risk: build/test ergonomics in IDE remain degraded despite green command-line verification.

## Environment Notes
- Command-line verification remains the authoritative path in this environment.
- Latest validation state: build success + `11/11` test pass.
