# Issues and Errors Log (v1.8 Upgrade)

## Date
2026-04-08

## Resolved During v1.8 Migration

### 1. Strict perf checks with zero samples produced weak first-read behavior
- Symptom: strict perf checks on fresh API instances could evaluate an empty sample window.
- Root cause: no latency samples exist before first runtime activity.
- Fix: added bounded strict-mode sample activation in `/perf` and exposed `sample_activation_seeded` field.
- Result: strict perf endpoint now provides deterministic non-empty contract evaluation path.

### 2. Missing API route for direct trace lookup
- Symptom: traces were queryable by CLI internals but no direct route existed for trace id retrieval.
- Root cause: API only exposed telemetry summary/filter routes.
- Fix: added `GET /trace/{trace_id}` with 404 on unknown IDs.
- Result: callers can resolve a known `trace_id` directly from API.

### 3. Planner output contract lacked decomposed score semantics
- Symptom: planner only emitted a single scalar score, reducing explainability for ranking.
- Root cause: `TaskPlanCandidate` had no structured score decomposition.
- Fix: added `PlanScore` (`relevance`, `execution_cost`, `success_probability`, `total`) and ranked `plans` envelope.
- Result: ranking is now deterministic and decomposed for downstream AI consumers.

### 4. CLI machine-output mode still mixed with logger output
- Symptom: machine parsing was noisy when runtime logs emitted alongside command responses.
- Root cause: logger had no runtime enable toggle and CLI had no global JSON-only mode.
- Fix: added `Logger::SetEnabled` and global CLI `--pure-json`, wired to JSON output paths.
- Result: CLI can emit clean JSON for automated callers.

### 5. API hardening coverage lagged behind new v1.8 routes/payloads
- Symptom: new route and payload fields were not asserted in integration suite.
- Root cause: tests reflected v1.7 surfaces only.
- Fix: expanded `integration_api_hardening` for `/state/ai` filter metadata, `/task/plan` score payload, `/trace/{trace_id}`, and strict perf activation metadata.
- Result: v1.8 additions are now regression-guarded.

## Current Known Risks (Non-Blocking)

### 1. Planner relevance is deterministic lexical/domain scoring
- Current behavior: no semantic embedding/model-based goal interpretation.
- Risk: highly abstract user goals may rank weakly without matching lexical cues.

### 2. Specialized adapter coverage is currently VS Code focused
- Current behavior: `VSCodeAdapter` adds specialization for Code context only.
- Risk: other high-value app domains still rely on generic adapters.

### 3. Duplicate ranked plan payloads for compatibility
- Current behavior: ranked plans appear in both `task_plan.plans` and top-level API `plans`.
- Risk: increased response size; can be compacted in a future contract version.

### 4. API server mode and runtime logs
- Current behavior: API commands can still print startup/runtime logs to stdout.
- Risk: API-shell wrappers should capture body from HTTP response rather than terminal stream.
