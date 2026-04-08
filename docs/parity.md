# IEE v1.9 Requirement Parity

## Objective Coverage

| Requirement | Expected | Implemented | Status |
|---|---|---|---|
| Action execution API | One-step deterministic action route | Added `POST /act` with structured result contract | Met |
| TargetResolver ranking | Deterministic ranking across label/planner/visibility/context/recency | Implemented `TargetResolver::Resolve` with bounded candidates and stable tie-breaks | Met |
| Ambiguity diagnostics | Ambiguous requests should return alternatives | Added `ambiguous_target` reason with candidate list and confidence scores | Met |
| Reveal-aware action execution | Hidden target actions must run through reveal -> execute -> verify | `ActionExecutor` uses `ExecutionContract` and returns reveal diagnostics | Met |
| Context-aware action behavior | Optional app/domain hints influence ranking deterministically | Added `ActionContextHints` and domain/app affinity scoring | Met |
| Action failure semantics | Explicit reason codes across parse/resolve/execute/verify | Added `reason` fields (`missing_value`, `target_not_found`, `verification_failed`, etc.) | Met |
| CLI action surface | One-step CLI with natural phrases and JSON modes | Added `iee act` with phrase parsing + `--json`/`--pure-json` | Met |
| Required integration tests | Resolve simple/ambiguous, reveal, set_value paths | Added `integration_action_interface` coverage | Met |
| Required scenario tests | VS Code palette, browser input+click, hidden menu | Added `scenario_action_interface` coverage | Met |
| Runtime compatibility | Preserve prior v1.8 and earlier contracts | Changes are additive; existing routes/flows retained | Met |

## Verified Runtime Behaviors
- `POST /act` resolves and executes one-step actions with deterministic target selection.
- `POST /act` returns `409` with `ambiguous_target` and candidate alternatives on near-tie matches.
- `POST /act` executes hidden targets through reveal contract and reports `reveal_used:true`.
- `POST /act` validates set-value intent requirements and reports `missing_value` deterministically.
- `iee act` supports natural input phrases and explicit flags with clean JSON output modes.
- All prior v1.8 runtime behaviors (planner scoring, `/state/ai` filters, trace route, strict perf activation, pure JSON mode) remain operational.

## Residual Risks (Non-Blocking)
- Action memory is in-process only and resets on restart; persisted memory store is future work.
- Domain affinity remains deterministic lexical heuristics and may need richer app-domain packs.
- Near-tie ambiguity thresholds are static and may need per-domain tuning once larger action corpora are available.
