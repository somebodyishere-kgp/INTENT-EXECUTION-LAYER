# Intent Execution Engine (IEE)

IEE is a native C++ system runtime that converts live OS/application state into a deterministic, queryable, and executable intent layer.

It is built as an execution substrate and control plane, not a UI product.

## Why IEE

Conventional automation stacks fail under ambiguity, latency spikes, stale state, and low observability. IEE addresses this with:

- deterministic intent resolution and execution
- bounded recovery and fallback behavior
- verifiable contract-based execution
- state projection for AI and external controllers
- evidence-driven telemetry and performance contracts

## Runtime Stage

This repository includes:

- IEE v1.0 to v1.9 foundations (intent runtime, control runtime, interaction graph, AI/task interfaces)
- IEE v2.0 (Phase 11): platformization across self-healing, semantic planning, adapter ecosystem metadata, temporal state history, policy controls, UCP envelopes, and expanded performance metrics

## IEE v2.0 Highlights (Phase 11)

1. Self-healing execution
- Added bounded deterministic recovery strategies in `SelfHealingExecutor`.
- Recovery order is fixed: `retry` -> `alternate_node` -> `fallback_reveal`.

2. Temporal unified state
- Added `TemporalStateEngine` with state history, transition metadata, and stability checks.
- Added frame consistency metrics (`expected`, `actual`, `skipped`, `score`).

3. Multi-step execution
- Added `IntentSequenceExecutor` and API route `POST /act/sequence`.
- Added `WorkflowExecutor` and API route `POST /workflow/run`.

4. Semantic interface
- Added `SemanticPlannerBridge` and route `POST /task/semantic`.
- Supports deterministic sequence generation for chained goals (`... then ...`).

5. Experience memory
- Added `ExecutionMemoryStore` for success/failure/fallback/latency memory.
- Resolver scoring now includes execution memory bias.

6. Adapter ecosystem
- Added `AdapterMetadata` and `AdapterRegistry::ListMetadata()`.
- Added route `GET /adapters` for deterministic adapter discovery.

7. Hybrid perception extensions
- Expanded environment perception output with lightweight text and grouped region signals.
- Added `lightweight_text_detections`, `grouped_region_count`, and `region_labels`.

8. Policy layer
- Added global policy store with route controls:
  - `GET /policy`
  - `POST /policy`
- Policy is enforced in `/act` and `/execute` paths.

9. Workflow orchestration
- Added deterministic sequence/workflow execution contracts in `core/platform`.

10. UCP protocol envelopes
- Added UCP wrappers:
  - `POST /ucp/act`
  - `GET /ucp/state`

11. Performance and scale metrics
- Added latency percentile route `GET /perf/percentiles` with `p50/p95/p99/p999`.
- Added frame coherency route `GET /perf/frame-consistency`.

12. Demo-ready API surfaces
- Added end-to-end deterministic routes for semantic planning, sequence execution, policy enforcement, and UCP state/action envelopes.

## Architecture

```text
Observer -> Intent Registry ---------> Execution Engine -> Adapter Runtime
   |              |                         ^                 |
   |              v                         |                 v
   |      Interaction Graph + Task Planner  |            Adapter Metadata
   |              |                         |                 |
   |              v                         |                 v
   +----> Environment Adapter -------> Action Interface -----> Execution Contract
                |                           |                     |
                |                           v                     v
                |                    Self-Healing Executor   Reveal -> Execute -> Verify
                v
         Unified EnvironmentState
                |
                +----> TemporalStateEngine (history/transitions/frame consistency)
                |
                +----> AIStateView / SemanticPlannerBridge / UCP envelopes

Telemetry <---------------- traces + latency + percentiles + persistence + failures

Platform Layer (core/platform): policy, memory, temporal state, sequence/workflow, semantic, UCP
```

## Repository Structure

```text
core/
  action/
  accessibility/
  capability/
  event/
  execution/
  intent/
  interaction/
  observer/
  platform/
  telemetry/
interface/
  api/
  cli/
  sdk/
docs/
tests/
```

## Build and Test

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## CLI Quickstart

```powershell
# list intents and capabilities
./build/Release/iee.exe list-intents
./build/Release/iee.exe capabilities --all

# one-step action interface
./build/Release/iee.exe act "open command palette" --pure-json
./build/Release/iee.exe act --action set_value --target "search bar" --value "github copilot" --domain browser --json

# planning and AI state
./build/Release/iee.exe state/ai --pure-json
./build/Release/iee.exe demo presentation --json
./build/Release/iee.exe demo browser --json

# graph and node diagnostics
./build/Release/iee.exe graph --json
./build/Release/iee.exe node <node_id>
./build/Release/iee.exe plan <node_id>
./build/Release/iee.exe reveal <node_id>

# telemetry and perf
./build/Release/iee.exe telemetry --json
./build/Release/iee.exe latency --json --limit 300
./build/Release/iee.exe perf --json --strict
./build/Release/iee.exe vision --json --limit 300

# API server
./build/Release/iee.exe api --port 8787
```

## API Quickstart

```powershell
# start API server
./build/Release/iee.exe api --port 8787
```

Available routes:

- `GET /health`
- `GET /intents`
- `GET /capabilities`
- `GET /capabilities/full`
- `GET /execution/memory`
- `GET /adapters`
- `GET /trace/{trace_id}`
- `GET /control/status`
- `GET /stream/state`
- `GET /state/ai`
- `GET /state/history`
- `GET /stream/frame`
- `GET /interaction-graph`
- `GET /interaction-node/{id}`
- `GET /stream/live`
- `GET /perf`
- `GET /perf/percentiles`
- `GET /perf/frame-consistency`
- `GET /policy`
- `GET /telemetry/persistence`
- `GET /ucp/state`
- `POST /execute`
- `POST /explain`
- `POST /act`
- `POST /act/sequence`
- `POST /workflow/run`
- `POST /task/semantic`
- `POST /task/plan`
- `POST /predict`
- `POST /policy`
- `POST /control/start`
- `POST /control/stop`
- `POST /stream/control`
- `POST /ucp/act`

## Example Payloads

Execute:

```json
{
  "action": "create",
  "path": "notes.txt"
}
```

One-step action:

```json
{
  "action": "activate",
  "target": "Command Palette",
  "context": {
    "app": "code",
    "domain": "generic"
  }
}
```

Sequence:

```json
{
  "steps": [
    { "action": "activate", "target": "Save" },
    { "action": "activate", "target": "Save" }
  ]
}
```

Semantic request:

```json
{
  "goal": "click Save then click Save",
  "context": {
    "domain": "generic"
  }
}
```

Policy update:

```json
{
  "allow_execute": "true",
  "allow_file_ops": "true",
  "allow_system_changes": "false"
}
```

## Adapter Ecosystem

Adapters are execution backends that expose capabilities and execute intents through a common contract.

- discovery: `GetCapabilities(...)`
- dispatch: `CanExecute(...)`, `Execute(...)`
- quality profile: `GetScore()`
- metadata discovery: `GetMetadata()`
- optional reactive hook: `Subscribe(...)`

See [docs/adapter_sdk.md](docs/adapter_sdk.md) for implementation guidance.

## Testing Strategy

The test suite includes unit, integration, scenario, and stress coverage.

Key validations include:

- deterministic resolver outcomes and action ambiguity handling
- reveal-contract and fallback behavior
- API hardening for malformed payloads and route contracts
- temporal/frame stream and graph delta consistency
- policy gating and semantic/sequence route behavior
- telemetry, persistence, latency, and percentile reporting

## Engineering Rules

This repository follows the IEE constitution:

- system over feature
- determinism over heuristics
- event-driven design
- strict modular boundaries
- mandatory documentation sync after each iteration

Primary docs:

- [docs/architecture.md](docs/architecture.md)
- [docs/status.md](docs/status.md)
- [docs/parity.md](docs/parity.md)
- [docs/issues_and_errors.md](docs/issues_and_errors.md)
- [docs/context_handoff.md](docs/context_handoff.md)
- [docs/ai_sdk.md](docs/ai_sdk.md)
- [docs/adapter_sdk.md](docs/adapter_sdk.md)

## License

See [LICENSE](LICENSE).
