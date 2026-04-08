# IEE v1.9 Architecture

## Purpose
IEE v1.9 extends the v1.8 AI-facing layer with a deterministic Action Interface Layer (AIL) that enables one-step natural action execution through `POST /act` and `iee act`. The system now includes action request parsing, deterministic target resolution, context-aware execution orchestration, and structured failure diagnostics.

## v1.9 Additions

### Action Interface Layer (AI Hands)
- Added new module:
  - `core/action/include/ActionInterface.h`
  - `core/action/src/ActionInterface.cpp`
- New primitives:
  - `ActionRequest` + `ActionContextHints`
  - `TargetResolver`
  - `ActionExecutor`
  - JSON parse/serialize helpers for action I/O

### Deterministic Target Resolution
- `TargetResolver` ranks candidates using additive deterministic signals:
  - label/fuzzy similarity
  - planner score (`TaskPlanner`)
  - visibility/reveal penalty
  - app/domain context affinity
  - recency and lightweight interaction memory
- Candidate search is bounded (`maxCandidates <= 8`) and sorted with deterministic tie-breaks.
- Ambiguity is surfaced as structured alternatives rather than non-deterministic auto-selection.

### Action Execution Orchestration
- `ActionExecutor` reuses existing runtime contracts:
  - `IntentRegistry` + UIG snapshot refresh
  - `TaskPlanner` for plan extraction
  - `ExecutionContract` for reveal -> execute -> verify
  - `ExecutionEngine` for adapter dispatch
- Returned result always carries trace semantics and execution diagnostics.

### Natural Action Semantics
- Action normalization supports deterministic synonyms:
  - `open/click/press/tap -> activate`
  - `type/enter -> set_value`
  - `goto/go_to/go -> navigate`
- `navigate` resolves to `set_value` on address-like targets with explicit value propagation.

## v1.8 Additions

### Intelligent Planning Score Contract
- `TaskPlanCandidate` now carries:
  - `PlanScore.relevance`
  - `PlanScore.executionCost`
  - `PlanScore.successProbability`
  - `PlanScore.total`
- `TaskPlanner` ranking order now uses `PlanScore.total` with deterministic tie-breaks.
- Planner JSON now includes:
  - per-candidate `plan_score`
  - ranked `plans: [{ plan, score }]` payload.

### AI State Filtering
- `GET /state/ai` adds deterministic filter projection:
  - `filter=interactive`
  - `filter=visible`
  - `filter=relevant&goal=...&domain=...&top_n=...`
- Filter payload includes mode, goal, domain, truncation metadata, and ranked node list.

### Adapter Specialization
- Added `VSCodeAdapter`:
  - wraps `UIAAdapter` behavior for VS Code contexts
  - uses specialized score profile for deterministic preference
  - preserves fallback path to generic adapters.

### Reveal Metadata v2
- Reveal execution now tracks:
  - `totalStepAttempts`
  - `fallbackUsed`
  - `fallbackStepCount`
- `/execute` now exposes reveal v2 fields and execution-level `used_fallback` metadata.

### Trace and Perf Surfaces
- Added `GET /trace/{trace_id}` API route backed by telemetry trace lookup.
- `GET /perf?strict=true` now seeds bounded synthetic latency samples when the sample window is empty and reports `sample_activation_seeded`.

### CLI Machine Mode
- Added global CLI option `--pure-json`:
  - suppresses logger output
  - forces structured JSON output across command handlers.

## Runtime Modules

### core/accessibility
- Captures full UIA tree (visible + hidden/offscreen/collapsed nodes).
- Preserves shortcut metadata and hierarchical parent-child relationships.
- Continues bounded menu probing for latent command discovery.

### core/interaction (v1.7)
- Stable identity contract:
  - `NodeId { stableId, signature }`
  - deterministic hashing from UI path, role/type, label, and automation identifier.
- Split node contracts:
  - `InteractionDescriptor` (structural/static shape)
  - `InteractionState` (frame-variant runtime state)
- Execution-aware graph contracts:
  - `ExecutionPlan` and `PlanStep`
  - `RevealStrategy` for hidden/offscreen/collapsed elements
  - `NodeIntentBinding` (node -> intent action + plan + reveal)
- Versioning and diff contracts:
  - `InteractionGraph.version`
  - `GraphDelta` add/update/remove lists + reset signaling.
- Task interface contracts:
  - `TaskRequest`
  - `TaskPlanCandidate`
  - `TaskPlanResult`
  - deterministic `TaskPlanner` scoring and stable `task_id` generation.
- AI projection contract:
  - `AIStateView` for compact action-oriented state summaries.

### core/execution (v1.7)
- Reveal hardening:
  - `RevealExecutor` executes reveal strategies with bounded retries and post-reveal checks.
- Execution guarantee layer:
  - `ExecutionContract` enforces `reveal -> execute -> verify` for contract-based dispatch.

### core/execution / observation
- `EnvironmentState.unifiedState` remains canonical merged observation model.
- Unified state now carries v1.6 UIG metadata in serialized graph payloads.
- Observation loop behavior remains non-blocking and deterministic.

### interface/api (v1.8)
- Existing UIG endpoints preserved and enriched:
  - `GET /interaction-graph`
  - `GET /interaction-node/{id}`
  - `GET /capabilities/full`
- New delta behavior:
  - `GET /interaction-graph?delta_since=<version>`
  - server keeps bounded graph history and returns `GraphDelta`
  - stale/missing base versions set `reset_required=true`.
- Node endpoint now surfaces execution-aware payloads:
  - `execution_plan`
  - `reveal_strategy`
  - `intent_binding`.
- New AI/task endpoints:
  - `GET /state/ai`
  - `POST /act`
  - `POST /task/plan` (planning-only)
  - `GET /trace/{trace_id}`
- Performance strict mode:
  - `GET /perf?strict=true` returns explicit strict status and can return conflict on budget breach.
  - strict mode can activate synthetic sample seed and report `sample_activation_seeded`.
- Execute contract metadata:
  - `POST /execute` responses now include contract/reveal stage details.
  - v1.8 adds reveal fallback metadata and execution fallback flag.

### interface/cli (v1.8)
- Existing commands preserved:
  - `iee graph`
  - `iee node <id>`
  - `iee capabilities --all`
- New execution-aware commands:
  - `iee plan <node_id>`
  - `iee reveal <node_id>`
- Graph command delta support:
  - `iee graph --delta_since <version>`
  - JSON mode emits graph + delta payload.
- AI/task commands:
  - `iee demo presentation|browser [--json] [--run]`
- Action interface commands:
  - `iee act "open command palette"`
  - `iee act --action set_value --target "search bar" --value "hello" [--app <hint>] [--domain <hint>]`
  - supports `--json` and `--pure-json`
- Global machine-output mode:
  - `--pure-json`
- Performance strict mode:
  - `iee perf --strict`

### interface/sdk (v1.7)
- In-process client contract:
  - `IEEClient::GetState()`
  - `IEEClient::GetStateAiJson()`
  - `IEEClient::Execute(...)` through `ExecutionContract`.

## Core Flow (v1.9)
1. Capture full UIA environment state.
2. Build deterministic UIG nodes with stable `NodeId` identities.
3. Build descriptor/state split for each node.
4. Derive reveal strategy for hidden/offscreen/collapsed nodes.
5. Generate execution plan and intent binding for every node.
6. Merge UIG with `ScreenState` into `UnifiedState`.
7. Project compact AI state view (`AIStateView`) for model consumers.
8. Plan high-level goals into deterministic candidates (`TaskPlanner`).
9. Rank candidates by structured `PlanScore` and expose ranked plan envelopes.
10. Enforce reveal-aware execution guarantees through `ExecutionContract`.
11. Resolve one-step action requests through deterministic `TargetResolver` ranking.
12. Execute resolved actions via `ActionExecutor` with contract-stage diagnostics.
13. Surface trace lookup, strict perf activation metadata, and machine-readable CLI outputs.
14. Expose AI/task/action/contract metadata through API/CLI/SDK.

## Determinism and Compatibility
- Existing v1.5.1 and v1.6 API/CLI routes are preserved.
- Legacy node fields remain serialized for compatibility while v1.7 contracts are additive.
- Graph signatures remain deterministic across equivalent frames.

## Validation Baseline
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`
