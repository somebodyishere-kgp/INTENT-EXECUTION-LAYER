# Issues and Errors Log (v1.9 Upgrade)

## Date
2026-04-08

## Resolved During v1.9 Migration

### 1. CLI phrase normalization helper missing during `iee act` integration
- Symptom: Release build failed with `C3861: 'ToAsciiLower': identifier not found` in `CliApp.cpp`.
- Root cause: new phrase parser reused helper name without defining CLI-local implementation.
- Fix: added deterministic ASCII lowercase helper in CLI source and included `<cctype>`.
- Result: CLI and all tests compile cleanly.

### 2. Natural action synonym `open` failed in scenario execution
- Symptom: `scenario_action_interface` failed on `action="open"` for command palette workflow.
- Root cause: action normalization passed raw lowercase action into `IntentActionFromString`, which does not include `open`.
- Fix: added explicit action synonym normalization (`open/click/press/tap -> activate`, `type/enter -> set_value`, `goto/go_to/go -> navigate`).
- Result: natural-language action scenarios pass deterministically.

### 3. CMake Tools build integration reported configure failure without diagnostics
- Symptom: `Build_CMakeTools` returned configure failure despite valid project files.
- Root cause: CMake Tools environment state mismatch in the local VS Code session.
- Fix: validated by direct `cmake -S . -B build` and continued with standard build/test commands.
- Result: build/test validation completed successfully while preserving source determinism.

### 4. Required action-layer coverage was missing initially
- Symptom: no dedicated coverage for `/act` resolve/ambiguity/reveal flows.
- Root cause: v1.9 module landed before dedicated integration/scenario harnesses were added.
- Fix: added `integration_action_interface` and `scenario_action_interface` and wired both in CMake/CTest.
- Result: required Phase 10 action behaviors are now regression guarded.

### 5. Strict perf checks with zero samples produced weak first-read behavior
- Symptom: strict perf checks on fresh API instances could evaluate an empty sample window.
- Root cause: no latency samples exist before first runtime activity.
- Fix: added bounded strict-mode sample activation in `/perf` and exposed `sample_activation_seeded` field.
- Result: strict perf endpoint now provides deterministic non-empty contract evaluation path.

### 6. Missing API route for direct trace lookup
- Symptom: traces were queryable by CLI internals but no direct route existed for trace id retrieval.
- Root cause: API only exposed telemetry summary/filter routes.
- Fix: added `GET /trace/{trace_id}` with 404 on unknown IDs.
- Result: callers can resolve a known `trace_id` directly from API.

### 7. Planner output contract lacked decomposed score semantics
- Symptom: planner only emitted a single scalar score, reducing explainability for ranking.
- Root cause: `TaskPlanCandidate` had no structured score decomposition.
- Fix: added `PlanScore` (`relevance`, `execution_cost`, `success_probability`, `total`) and ranked `plans` envelope.
- Result: ranking is now deterministic and decomposed for downstream AI consumers.

### 8. CLI machine-output mode still mixed with logger output
- Symptom: machine parsing was noisy when runtime logs emitted alongside command responses.
- Root cause: logger had no runtime enable toggle and CLI had no global JSON-only mode.
- Fix: added `Logger::SetEnabled` and global CLI `--pure-json`, wired to JSON output paths.
- Result: CLI can emit clean JSON for automated callers.

### 9. API hardening coverage lagged behind new v1.8 routes/payloads
- Symptom: new route and payload fields were not asserted in integration suite.
- Root cause: tests reflected v1.7 surfaces only.
- Fix: expanded `integration_api_hardening` for `/state/ai` filter metadata, `/task/plan` score payload, `/trace/{trace_id}`, and strict perf activation metadata.
- Result: v1.8 additions are now regression-guarded.

## Current Known Risks (Non-Blocking)

### 1. Action ranking memory is process-local
- Current behavior: action memory is maintained in an in-memory map keyed by query/domain/app.
- Risk: resolution recency benefits reset on process restart.

### 2. Planner relevance is deterministic lexical/domain scoring
- Current behavior: no semantic embedding/model-based goal interpretation.
- Risk: highly abstract user goals may rank weakly without matching lexical cues.

### 3. Specialized adapter coverage is currently VS Code focused
- Current behavior: `VSCodeAdapter` adds specialization for Code context only.
- Risk: other high-value app domains still rely on generic adapters.

### 4. Duplicate ranked plan payloads for compatibility
- Current behavior: ranked plans appear in both `task_plan.plans` and top-level API `plans`.
- Risk: increased response size; can be compacted in a future contract version.

### 5. API server mode and runtime logs
- Current behavior: API commands can still print startup/runtime logs to stdout.
- Risk: API-shell wrappers should capture body from HTTP response rather than terminal stream.
