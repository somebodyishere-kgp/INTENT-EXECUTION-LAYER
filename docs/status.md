# IEE v1.9 Status

## Date
2026-04-08

## Current State
IEE has been upgraded to v1.9 with an Action Interface Layer (AI Hands) for deterministic one-step action execution via API and CLI.

The runtime now supports:

- stateless in-process SDK access (`IEEClient`)
- deterministic planning-only task decomposition (`TaskPlanner`)
- structured `PlanScore` ranking (`relevance`, `execution_cost`, `success_probability`, `total`)
- reveal-aware execution guarantees (`ExecutionContract` + `RevealExecutor`)
- compact model-facing state projection (`AIStateView`)
- query-time AI state filtering (`interactive`, `visible`, `relevant`)
- VS Code adapter specialization (`VSCodeAdapter`)
- direct trace retrieval route (`GET /trace/{trace_id}`)
- machine-output CLI mode (`--pure-json`)
- strict latency contract reporting in both API and CLI surfaces
- one-step action execution route (`POST /act`)
- deterministic target resolver scoring across planner/label/visibility/context/recency
- CLI natural action command (`iee act ...`) with JSON and pure-JSON modes

## Completed v1.9 Work
- Action interface core:
  - added `core/action` module with `ActionRequest`, `TargetResolver`, `ActionExecutor`
  - added deterministic action request parsing and response serialization
  - added interaction-memory weighting for repeated successful target resolutions
- One-step action API:
  - added `POST /act`
  - added structured parse failures (`missing_action`, `missing_target`, etc.)
  - added deterministic status mapping for ambiguous/bad-request/internal failure classes
- One-step action CLI:
  - added `iee act` command handler
  - supports phrase parsing (`click/open/type/navigate/go to`) and explicit options
  - supports `--json` and global `--pure-json`
- Validation tests:
  - added `integration_action_interface`
  - added `scenario_action_interface`
  - covered required paths:
    - simple resolve/execute
    - ambiguous target handling
    - hidden reveal execution
    - `set_value` flow
    - natural-language scenarios (VS Code palette, browser input+click, hidden menu)

## Prior v1.8 Work (Retained)
- Task interface:
  - added `PlanScore` to planner output
  - ranking now uses deterministic `PlanScore.total`
  - `TaskPlanner` JSON now includes `plan_score` and ranked `plans: [{ plan, score }]`
- AI state filtering:
  - upgraded `GET /state/ai` with deterministic filter modes (`interactive`, `visible`, `relevant`)
  - added relevant-goal query controls (`goal`, `domain`, `top_n`, `include_hidden`)
- Adapter specialization:
  - added `VSCodeAdapter` with VS Code context gating and tuned score profile
  - preserved fallback to generic adapter execution path
- Reveal and execution metadata v2:
  - reveal now tracks `totalStepAttempts`, `fallbackUsed`, `fallbackStepCount`
  - `/execute` now returns `used_fallback` and reveal v2 metadata fields
- Trace API:
  - added `GET /trace/{trace_id}`
- Strict perf activation:
  - `GET /perf?strict=true` now seeds bounded sample metadata when no samples exist
  - added `sample_activation_seeded` response field
- CLI machine mode:
  - added global `--pure-json`
  - enabled clean JSON output for execute/explain/trace/inspect/list-intents and existing JSON-capable handlers
- Validation:
  - extended `integration_api_hardening` for `/state/ai` filtering, `/task/plan` score payloads, `/trace/{trace_id}`, and strict perf activation metadata

## Retained Prior Behavior
- v1.5.1 through v1.7 routes remain available.
- UIG stable IDs, descriptor/state split, reveal contracts, and graph delta contracts remain additive-compatible.
- Telemetry persistence, control runtime, and stream endpoints remain intact.

## Verification
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Release`
- Tests: `ctest --test-dir build -C Release --output-on-failure`

## Real Demo Evidence (Current Session)
- CLI pure JSON state:
  - `iee state/ai --pure-json` returned structured state with no logger noise.
- Presentation plan demo:
  - `iee demo presentation --pure-json` returned ranked candidates with `plan_score` and `plans` payloads.
- Browser plan demo:
  - `iee demo browser --pure-json` returned ranked candidates with `plan_score` and `plans` payloads.
- Hidden-aware relevant filter demo:
  - `GET /state/ai?filter=relevant&goal=export%20menu&domain=presentation&top_n=5&include_hidden=true` returned ranked filter nodes.
- Trace route demo:
  - `POST /execute` followed by `GET /trace/{trace_id}` returned trace payload for the same execution.
- Strict perf activation demo:
  - `GET /perf?strict=true` returned `sample_activation_seeded:true` with non-zero contract sample count.

## Remaining Non-Blocking Gaps
- Action memory is process-local and non-persistent; optional persisted action memory remains future work.
- Domain affinity heuristics are deterministic and lightweight; deeper domain packs can be added per app family.
- Reveal actions remain generic activation primitives; deeper app-specific reveal actions can be expanded.
