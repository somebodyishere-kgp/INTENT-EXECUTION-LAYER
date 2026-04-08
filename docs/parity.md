# IEE v1.8 Requirement Parity

## Objective Coverage

| Requirement | Expected | Implemented | Status |
|---|---|---|---|
| Intelligent plan ranking | Structured score model with relevance/cost/success and deterministic total rank | Added `PlanScore` and ranking by `PlanScore.total` | Met |
| Plan output contract | Ranked `plans: [{plan, score}]` envelope | Added `TaskPlanner::SerializeRankedPlansJson` and API `plans` payload | Met |
| AI state filter modes | Interactive/visible/relevant views with goal-aware truncation | Added `GET /state/ai` filter metadata and ranked node payload | Met |
| Adapter specialization | Context-specific adapter selection with deterministic fallback | Added `VSCodeAdapter` and retained generic fallback path | Met |
| Reveal metadata v2 | Reveal fallback and attempt metadata in execution results | Added reveal fields and surfaced in `/execute` response | Met |
| Trace retrieval API | Direct trace lookup route | Added `GET /trace/{trace_id}` with 404 semantics | Met |
| Pure machine CLI output | Flag to suppress logs and force JSON | Added global `--pure-json` and JSON-path handling across CLI handlers | Met |
| Strict perf activation | Non-zero strict perf sample path on fresh windows | Added strict sample seeding and `sample_activation_seeded` metadata | Met |
| Integration hardening | Regression coverage for new contracts | Extended `integration_api_hardening` assertions for v1.8 routes/payloads | Met |
| Backward compatibility | Preserve existing v1.5.1-v1.7 routes/contracts | Existing APIs retained; v1.8 changes are additive | Met |

## Verified Runtime Behaviors
- `POST /task/plan` now returns both legacy candidates and ranked `plans` score envelopes.
- `GET /state/ai` returns filter metadata and deterministic ranked node projections.
- `POST /execute` returns execution fallback and reveal v2 metadata fields.
- `GET /trace/{trace_id}` returns the persisted in-memory trace payload for the matching execution.
- `GET /perf?strict=true` now reports `sample_activation_seeded` when bootstrapping empty sample windows.
- CLI `--pure-json` mode returns machine-readable output without logger noise.

## Residual Risks (Non-Blocking)
- `VSCodeAdapter` target heuristics are deterministic but broad; app-specific specialization can be tightened further.
- Relevance mode is lexical/domain-based and intentionally avoids heavyweight semantic models.
- `TaskPlanResult` now carries both `task_plan.plans` and top-level API `plans` for compatibility, which duplicates payload size.
