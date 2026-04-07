# IEE v1.7 Architecture

## Purpose
IEE v1.7 adds an AI-facing interface layer on top of the v1.6 production execution graph. The system now includes stateless SDK access (`IEEClient`), deterministic task planning (`TaskPlanner`), reveal-hardening execution contracts (`ExecutionContract`), and compact model-facing state projection (`AIStateView`).

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

### interface/api (v1.7)
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
  - `POST /task/plan` (planning-only)
- Performance strict mode:
  - `GET /perf?strict=true` returns explicit strict status and can return conflict on budget breach.
- Execute contract metadata:
  - `POST /execute` responses now include contract/reveal stage details.

### interface/cli (v1.7)
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
- Performance strict mode:
  - `iee perf --strict`

### interface/sdk (v1.7)
- In-process client contract:
  - `IEEClient::GetState()`
  - `IEEClient::GetStateAiJson()`
  - `IEEClient::Execute(...)` through `ExecutionContract`.

## Core Flow (v1.7)
1. Capture full UIA environment state.
2. Build deterministic UIG nodes with stable `NodeId` identities.
3. Build descriptor/state split for each node.
4. Derive reveal strategy for hidden/offscreen/collapsed nodes.
5. Generate execution plan and intent binding for every node.
6. Merge UIG with `ScreenState` into `UnifiedState`.
7. Project compact AI state view (`AIStateView`) for model consumers.
8. Plan high-level goals into deterministic candidates (`TaskPlanner`).
9. Enforce reveal-aware execution guarantees through `ExecutionContract`.
10. Expose AI/task/contract metadata through API/CLI/SDK.

## Determinism and Compatibility
- Existing v1.5.1 and v1.6 API/CLI routes are preserved.
- Legacy node fields remain serialized for compatibility while v1.7 contracts are additive.
- Graph signatures remain deterministic across equivalent frames.

## Validation Baseline
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`
