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

## v1.3 Highlights

- Environment abstraction:
  - `EnvironmentAdapter`
  - `EnvironmentState`
  - live and mock adapters
- High-frequency observation pipeline:
  - dedicated thread
  - double-buffer state handoff
- Lightweight perception primitives:
  - dominant surface classification
  - focus/occupancy ratios
  - UI region density/focus
- Macro control composition:
  - ordered `ActionSequence`
  - compact DSL parsing
- Dual synchronized pipelines:
  - observation lane + execution lane
- Latency phase profiling:
  - observation/perception/queue/execution/verification/total
- Streaming API:
  - `GET /stream/state`
  - `POST /stream/control`

## Architecture

```text
Observer -> Intent Registry ---------> Execution Engine -> Adapter Runtime
   |              |                         ^                 |
   |              v                         |                 v
   +------> Environment Adapter -> Observation Pipeline -> Control Runtime
                                  |                      |
                                  +------> Perception ---+

Telemetry <---------------- traces + latency breakdowns + persistence
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
- `POST /execute`
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
- stream state/control and macro execution paths

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

## License

See [LICENSE](LICENSE).
