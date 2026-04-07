# IEE v1.6 Architecture

## Purpose
IEE v1.6 upgrades the Unified Interaction Graph (UIG) from a discovery graph into a production execution graph. Nodes now expose deterministic stable identity, descriptor/state separation, execution plans, reveal strategies for hidden UI, and versioned graph deltas.

## Runtime Modules

### core/accessibility
- Captures full UIA tree (visible + hidden/offscreen/collapsed nodes).
- Preserves shortcut metadata and hierarchical parent-child relationships.
- Continues bounded menu probing for latent command discovery.

### core/interaction (v1.6)
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

### core/execution / observation
- `EnvironmentState.unifiedState` remains canonical merged observation model.
- Unified state now carries v1.6 UIG metadata in serialized graph payloads.
- Observation loop behavior remains non-blocking and deterministic.

### interface/api (v1.6)
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

### interface/cli (v1.6)
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

## Core Flow (v1.6)
1. Capture full UIA environment state.
2. Build deterministic UIG nodes with stable `NodeId` identities.
3. Build descriptor/state split for each node.
4. Derive reveal strategy for hidden/offscreen/collapsed nodes.
5. Generate execution plan and intent binding for every node.
6. Merge UIG with `ScreenState` into `UnifiedState`.
7. Expose full graph and node-level execution metadata through API/CLI.
8. Serve versioned graph deltas for incremental consumers.

## Determinism and Compatibility
- Existing v1.5.1 API/CLI routes are preserved.
- Legacy node fields remain serialized for compatibility while v1.6 contracts are additive.
- Graph signatures remain deterministic across equivalent frames.

## Validation Baseline
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`
