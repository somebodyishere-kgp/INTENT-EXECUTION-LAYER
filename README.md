# Intent Execution Engine (IEE)

IEE is a native C++ system runtime that converts live OS/application state into a deterministic, queryable, and executable intent layer.

It is built as an execution substrate and control plane, not a UI product.

## Why IEE

Conventional automation stacks fail under ambiguity, latency spikes, stale state, and low observability.
IEE addresses this by enforcing:

- deterministic intent resolution
- verifiable adapter execution
- event-aware real-time control
- bounded runtime behavior
- evidence-driven telemetry and latency profiling

## Current Runtime Stage

This repository now includes:

- IEE v1: deterministic intent execution core
- IEE v1.1: telemetry, reliability scoring, and hardened API baseline
- IEE v1.2: control runtime, cache invalidation v2, persisted telemetry
- IEE v1.3: environment-aware real-time control
- IEE v1.4: decision + feedback + prediction layer
- IEE v1.5: screen perception + unified screen state
- IEE v1.5.1: unified interaction graph + hidden UI exposure
- IEE v1.6: production execution-aware UIG + graph versioning/deltas
- IEE v1.7: AI SDK, task planning interface, reveal hardening, and execution contracts
- IEE v1.8: intelligent plan scoring, AI state filters, adapter specialization, trace route, and pure JSON CLI mode

## v1.8 Highlights

- Intelligent planning score model:
  - `PlanScore { relevance, execution_cost, success_probability, total }`
  - planner output now includes ranked `plans: [{ plan, score }]` payloads
- AI state filter modes on `GET /state/ai`:
  - `filter=interactive`
  - `filter=visible`
  - `filter=relevant&goal=...&domain=...&top_n=...`
- Adapter specialization:
  - added `VSCodeAdapter` for VS Code-context UI specialization with deterministic fallback to generic adapters
- Reveal metadata v2:
  - execution responses now include reveal fallback metadata (`reveal_total_step_attempts`, `reveal_fallback_used`, `reveal_fallback_step_count`)
- Trace API:
  - added `GET /trace/{trace_id}`
- Machine-output mode:
  - added global CLI flag `--pure-json` to force structured output and suppress runtime logger noise
- Strict perf activation:
  - `GET /perf?strict=true` now seeds bounded synthetic latency sample metadata when sample window is empty (`sample_activation_seeded`)

## v1.7 Highlights

- AI-facing SDK:
  - `IEEClient` with stateless `getState` and `execute` entrypoints for in-process integrations
- Task interface:
  - `TaskRequest` + deterministic `TaskPlanner`
  - new planning-only API route: `POST /task/plan`
- Reveal and contract hardening:
  - `RevealExecutor` executes reveal plans with retries and verification checks
  - `ExecutionContract` enforces `reveal -> execute -> verify` when node metadata is available
- AI state projection:
  - `AIStateView` with compact, model-friendly state summaries
  - new API route: `GET /state/ai`
- Latency contract strict mode:
  - CLI: `iee perf --strict`
  - API: `GET /perf?strict=true` with explicit strict pass/fail fields
- Developer experience:
  - scenario demos: `iee demo presentation` and `iee demo browser`
  - SDK example source: `interface/sdk/examples/ai_client_example.cpp`
  - new task-interface scenario coverage in `scenario_task_interface`

## v1.6 Highlights

- Stable node identity:
  - deterministic `NodeId { stableId, signature }` for cross-frame consistency
  - hash-based identity from UI path/role/label/automation context
- Execution-aware graph model:
  - `InteractionDescriptor` + `InteractionState` split
  - per-node `ExecutionPlan`, `RevealStrategy`, and `NodeIntentBinding`
- API expansion:
  - `GET /interaction-graph` now includes graph version metadata
  - `GET /interaction-graph?delta_since=<version>` returns bounded-history graph delta payloads
  - `GET /interaction-node/{id}` now includes execution plan/reveal/binding payloads
- CLI expansion:
  - `iee graph --delta_since <version>`
  - `iee plan <node_id>`
  - `iee reveal <node_id>`
- Validation expansion:
  - stable identity + reveal/plan/delta tests in unit/scenario/API hardening suites

## v1.5.1 Highlights

- Full-tree UI ingestion:
  - captures visible + hidden/offscreen/collapsed/disabled nodes
  - bounded menu probe path for latent menu/combobox child discovery
- Unified Interaction Graph (UIG):
  - deterministic node IDs and signatures
  - explicit edge model and latent command nodes (shortcut/access-key derived)
  - deterministic node-to-intent mapping helpers
- Unified runtime state:
  - `UnifiedState` merges `ScreenState` + `InteractionGraph`
  - `/stream/state` includes `unified_state`
- API expansion:
  - `GET /interaction-graph`
  - `GET /interaction-node/{id}`
  - `GET /capabilities/full`
- CLI expansion:
  - `iee graph`
  - `iee node <id>`
  - `iee capabilities --all`
- Additional validation:
  - `unit_interaction_graph`
  - `scenario_uig_hidden_exposure`
  - API hardening coverage for UIG endpoints

## v1.5 Highlights

- Unified screen perception layer:
  - `ScreenCaptureEngine` with DXGI Desktop Duplication first path
  - deterministic fallback when duplication is unavailable
  - `VisualDetector` heuristics (edge/segment/color/text-like cues)
  - `ScreenStateAssembler` merge of UIA + visual + cursor with stable IDs
- Environment/runtime upgrade:
  - `EnvironmentState` now includes `screenFrame`, `screenState`, and `visionTiming`
  - observation metrics now include latest frame id and vision timing summaries
  - control runtime status now includes vision timing and observation FPS
- Frame streaming API:
  - `GET /stream/frame` for full or delta frame state transport
  - bounded history with reset signaling for stale delta cursors
- Vision telemetry:
  - capture/detection/merge/total latency aggregation
  - dropped/simulated frame accounting
  - `iee vision` CLI command
- v1.4 capabilities retained:
  - decision + feedback + prediction surfaces
  - macro v2 control flow (`loop`, `if_visible`)
  - performance contract (`GET /perf`, `iee perf`)
  - live SSE stream (`GET /stream/live`)

## Architecture

```text
Observer -> Intent Registry ---------> Execution Engine -> Adapter Runtime
   |              |                         ^                 |
   |              v                         |                 v
  +------> Environment Adapter -> Observation Pipeline -> Control Runtime
                       |            |         |         \
                       |            |         |          -> Feedback + Corrections
                       |            |         +----------> Decision Provider (optional)
                       |            v
                       |      Screen Capture (DXGI/fallback)
                       |            |
                       |      Visual Detector
                       |            |
                +----> Unified ScreenState (UIA + visual + cursor)
                        |
                        +----> Interaction Graph Builder (full UI tree + commands)
                                |
                                +----> UnifiedState (screen + interaction graph)

Predictor hook -> /predict and runtime prediction surfaces

Telemetry <---------------- traces + latency breakdowns + vision metrics + persistence
```

## Repository Structure

```text
core/
	accessibility/
	capability/
	event/
	execution/
	intent/
	observer/
	telemetry/
interface/
	api/
	cli/
docs/
tests/
```

## Build Requirements

- Windows 10/11
- CMake 3.20+
- MSVC with C++20 support
- Win32 + COM + UIA development environment

## Build and Test

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## CLI Usage

```powershell
# list discovered intents
./build/Debug/iee.exe list-intents

# execute an intent
./build/Debug/iee.exe execute activate --target "Save"

# inspect runtime state
./build/Debug/iee.exe inspect
./build/Debug/iee.exe state/ai --pure-json

# inspect interaction graph and a node mapping
./build/Debug/iee.exe graph
./build/Debug/iee.exe graph --json
./build/Debug/iee.exe node <node_id>

# inspect execution plan + reveal strategy for a node
./build/Debug/iee.exe plan <node_id>
./build/Debug/iee.exe reveal <node_id>

# inspect graph delta from a prior graph version
./build/Debug/iee.exe graph --delta_since 1001 --json

# list full capabilities including hidden nodes
./build/Debug/iee.exe capabilities --all

# explain deterministic resolution ranking
./build/Debug/iee.exe explain --action activate --target "Save"

# telemetry summary and persistence state
./build/Debug/iee.exe telemetry
./build/Debug/iee.exe telemetry --persistence

# latency breakdown profiler
./build/Debug/iee.exe latency
./build/Debug/iee.exe latency --json --limit 300

# performance contract snapshot
./build/Debug/iee.exe perf
./build/Debug/iee.exe perf --json --target_ms 12 --limit 300
./build/Debug/iee.exe perf --target_ms 12 --strict

# deterministic task planning demos
./build/Debug/iee.exe demo presentation
./build/Debug/iee.exe demo browser --json
./build/Debug/iee.exe demo presentation --run

# force machine-readable JSON output
./build/Debug/iee.exe execute create --path notes.txt --pure-json

# vision pipeline metrics
./build/Debug/iee.exe vision
./build/Debug/iee.exe vision --json --limit 300

# recent traces or a specific trace
./build/Debug/iee.exe trace
./build/Debug/iee.exe trace --id <trace_id>

# start local API server
./build/Debug/iee.exe api --port 8787
```

## API Usage

```powershell
# start local API server
./build/Debug/iee.exe api --port 8787
```

Available routes:

- `GET /health`
- `GET /intents`
- `GET /capabilities`
- `GET /capabilities/full`
- `GET /interaction-graph`
- `GET /interaction-node/{id}`
- `GET /telemetry/persistence`
- `GET /trace/{trace_id}`
- `GET /control/status`
- `GET /stream/state`
- `GET /state/ai`
- `GET /stream/frame`
- `GET /stream/live`
- `GET /perf`
- `POST /execute`
- `POST /task/plan`
- `POST /predict`
- `POST /explain`
- `POST /control/start`
- `POST /control/stop`
- `POST /stream/control`

Example execute payload:

```json
{
	"action": "create",
	"path": "notes.txt"
}
```

Example task-plan payload:

```json
{
  "goal": "export hidden menu",
  "target": "Export",
  "domain": "presentation",
  "allow_hidden": "true",
  "max_plans": "3"
}
```

Example stream-control macro payload:

```json
{
	"sequence": "create|macro_a.txt;move|macro_a.txt|macro_b.txt;delete|macro_b.txt"
}
```

Example frame stream query:

```text
GET /stream/frame?mode=delta&since=42
```

Example interaction-graph delta query:

```text
GET /interaction-graph?delta_since=1001
```

Example AI-state relevant filter query:

```text
GET /state/ai?filter=relevant&goal=export%20menu&domain=presentation&top_n=5&include_hidden=true
```

Example prediction payload:

```json
{
  "action": "create",
  "path": "notes.txt"
}
```

## Adapter Model

Adapters are execution backends that expose capabilities and execute intents through a common contract.

- discovery: `GetCapabilities(...)`
- dispatch: `CanExecute(...)`, `Execute(...)`
- quality profile: `GetScore()`
- optional reactive hook: `Subscribe(...)`

See [docs/adapter_sdk.md](docs/adapter_sdk.md) for implementation guidance.

## Testing Strategy

The suite includes unit, integration, scenario, and stress coverage.

Key validations include:

- deterministic resolver outcomes
- adapter reliability behavior
- failure injection and fallback behavior
- API hardening under malformed payloads
- high-frequency execution latency distribution
- observation pipeline behavior
- unified screen-state capture/detect/merge behavior
- interaction graph hidden/offscreen/command extraction behavior
- unified-state interaction graph consistency across frames
- frame stream full/delta API behavior
- stream state/control and macro execution paths
- live SSE stream route and performance contract endpoint
- closed-loop decision/feedback/correction behavior

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

## Repository About

Suggested GitHub About description:

`Deterministic C++ intent execution runtime with AI-facing SDK, production execution-aware interaction graphs, real-time control, and low-latency observability APIs.`

Suggested topics:

`intent-execution`, `automation-runtime`, `c-plus-plus`, `windows`, `deterministic-systems`, `real-time-control`, `telemetry`, `screen-perception`, `interaction-graph`, `accessibility`, `ai-sdk`, `task-planning`

## License

See [LICENSE](LICENSE).
