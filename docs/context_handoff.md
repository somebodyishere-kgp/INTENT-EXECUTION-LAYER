# IEE Context Handoff (v1.8)

## 1. What Are We Building
The Intent Execution Engine (IEE): a deterministic native execution layer that maps live OS/application state into executable intent space.

v1.8 extends v1.7 AI-facing interfaces with structured planning scores, AI state filter modes, adapter specialization, trace lookup, and machine-output CLI mode.

## 2. Current State
Completed in v1.8:
- Planner score model:
  - added `PlanScore` (`relevance`, `execution_cost`, `success_probability`, `total`)
  - candidate ranking now sorts by `PlanScore.total`
  - planner output includes ranked `plans: [{ plan, score }]`
- AI state filter modes:
  - `GET /state/ai` now supports `interactive`, `visible`, `relevant`
  - added query controls: `goal`, `domain`, `top_n`, `include_hidden`
- Adapter specialization:
  - added `VSCodeAdapter` and registered it ahead of generic UI adapters
- Reveal metadata v2:
  - added `totalStepAttempts`, `fallbackUsed`, `fallbackStepCount` in reveal execution
  - `/execute` now includes reveal v2 metadata and execution-level `used_fallback`
- Trace route:
  - added `GET /trace/{trace_id}`
- CLI machine mode:
  - added global `--pure-json`
  - integrated clean JSON output paths for command responses
- Strict perf activation:
  - `/perf?strict=true` now seeds sample when empty and reports `sample_activation_seeded`
- Tests:
  - extended `integration_api_hardening` for v1.8 APIs/payload fields

Retained behavior:
- Existing v1.5.1-v1.7 APIs and routes remain additive-compatible.
- Existing telemetry/control runtime/event flow remains intact.

## 3. Last Work Done
- Implemented v1.8 code paths across:
  - `TaskInterface.*`
  - `IntentApiServer.cpp`
  - `Adapter.*`
  - `ExecutionEngine.cpp`
  - `RevealExecutor.*`
  - `Logger.*`
  - `CliApp.*`
  - `main.cpp`
- Added/updated tests in `tests/integration_api_hardening.cpp`.
- Built in Release and ran full CTest suite (17/17 passed).
- Ran live demos for:
  - `iee state/ai --pure-json`
  - `iee demo presentation --pure-json`
  - `iee demo browser --pure-json`
  - `GET /state/ai?filter=relevant...`
  - `POST /execute` + `GET /trace/{trace_id}`
  - `GET /perf?strict=true` sample activation path

## 4. Current Problem
No active blocker.

Known non-blocking considerations:
1. Relevance filter and planner remain lexical/domain deterministic and non-semantic by design.
2. Adapter specialization currently targets VS Code; additional app-specific adapters can be added.
3. Ranked plan payload duplication exists (`task_plan.plans` and top-level `plans`) for compatibility.

## 5. Next Plan
1. Add adapter specializations for browser and office-class workflows.
2. Add optional planner action guardrails (allowlist/denylist and max reveal-step constraints).
3. Add route-level JSON shape tests for `--pure-json` CLI commands.
4. Introduce a contract version flag for compact planner payload mode.

## 6. Key Decisions Taken
- Keep v1.8 fully additive to avoid breaking existing API/CLI consumers.
- Preserve deterministic scoring and avoid model-heavy ranking in core runtime.
- Use adapter specialization with fallback, not hard adapter pinning.
- Add strict perf sample activation only in strict mode to avoid polluting normal telemetry windows.
- Enforce machine-output behavior at CLI layer via global logger toggle and per-command JSON serialization.

## 7. Multi-Agent Contribution
- Primary implementation agent: applied all code changes, build/test validation, and doc synchronization.
- Explore sub-agent: used for initial architecture reconnaissance and identifying candidate patch points before implementation.
