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
- `GET /telemetry/persistence`
- `GET /control/status`
- `GET /stream/state`
- `GET /stream/frame`
- `GET /stream/live`
- `GET /perf`
- `POST /execute`
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

## Repository About

Suggested GitHub About description:

`Deterministic C++ intent execution runtime with real-time control, unified screen state, and low-latency observability APIs.`

Suggested topics:

`intent-execution`, `automation-runtime`, `c-plus-plus`, `windows`, `deterministic-systems`, `real-time-control`, `telemetry`, `screen-perception`

## License

See [LICENSE](LICENSE).
