# IEE v1.7 Requirement Parity

## Objective Coverage

| Requirement | Expected | Implemented | Status |
|---|---|---|---|
| AI SDK interface | Stateless in-process AI client with state + execute surfaces | Added `IEEClient` (`GetState`, `GetStateAiJson`, `Execute`) | Met |
| Task abstraction | Deterministic planning-only request/response contract | Added `TaskRequest`, `TaskPlanner`, `TaskPlanResult`, `TaskPlanCandidate` | Met |
| Planning API | `POST /task/plan` must produce plan only and not execute | Added planning-only endpoint with deterministic planner output | Met |
| Reveal hardening | Bounded reveal retries and reveal verification | Added `RevealExecutor` with step retries + verification checks | Met |
| Execution guarantee | Enforce reveal -> execute -> verify sequencing | Added `ExecutionContract` and wired `/execute` through contract path | Met |
| AI state projection | Compact state for models via API | Added `AIStateView` + `GET /state/ai` | Met |
| Latency strict contract | Strict budget pass/fail surfaced in CLI/API | Added `iee perf --strict` and `/perf?strict=true` strict status fields | Met |
| Scenario tests | Presentation/browser/hidden-menu planning coverage | Added `scenario_task_interface` | Met |
| API hardening coverage | New route validation and strict contract checks | Extended `integration_api_hardening` for `/state/ai`, `/task/plan`, strict perf | Met |
| Developer docs | Dedicated AI SDK/task docs + required core docs sync | Added `docs/ai_sdk.md` and updated architecture/status/parity/issues/context docs | Met |
| Non-regression | Preserve existing v1.5.1/v1.6 behavior and routes | Existing endpoints retained; v1.7 additions are additive | Met |

## Verified Runtime Behaviors
- `POST /task/plan` returns deterministic plan candidates and does not execute actions.
- `GET /state/ai` returns compact `interaction_summary`, dominant actions, and hidden-node digest.
- `/execute` responses now include contract stage + reveal metadata.
- `GET /perf?strict=true` includes strict status fields and can return conflict when budget checks fail.
- CLI supports `iee demo presentation|browser` and `iee perf --strict`.

## Residual Risks (Non-Blocking)
- Reveal actions currently map to generic activation primitives; adapter-specific reveal actions can be added.
- Task ranking remains deterministic keyword/domain based (not semantic intent reasoning).
- API graph delta history remains bounded for memory safety and can require reset.
