# Intent Execution Engine (IEE)

IEE is a native C++ system-level runtime that transforms live OS and application context into a deterministic, queryable, and executable intent layer.

It is built as a control-plane substrate, not a UI app or assistant.

## Why IEE

Modern automation stacks often break under ambiguity, latency spikes, and hidden side effects.
IEE addresses this by enforcing:

- deterministic intent resolution
- strict execution validation and verification
- adapter-based extensibility
- event-driven updates with bounded runtime behavior
- real-time observability and traceability

## Current Runtime Stage

This repository currently includes:

- IEE v1: deterministic intent execution core
- IEE v1.1: telemetry, reliability scoring, hardened API, and real-time readiness primitives

## Core Capabilities

- Capability graph extraction from live observer snapshots
- Intent schema v2 (target, params, context, constraints)
- Deterministic resolver with ambiguity handling
- Adapter execution model:
	- UIA adapter
	- filesystem adapter
	- input/control adapter foundation
- Execution recovery:
	- retry
	- fallback
	- timeout gate
- Event model with priority levels (`HIGH`, `MEDIUM`, `LOW`)
- Telemetry traces and adapter performance metrics
- Local API and operator CLI

## Architecture

```text
Observer -> Capability Graph -> Intent Registry/Resolver -> Execution Engine -> Verification
		 ^                |                  |                     |                 |
		 |                |                  |                     |                 v
 Event Watchers ------+--------> Telemetry + Priority Events <-+---------- Adapter Runtime
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

# telemetry summary
./build/Debug/iee.exe telemetry

# recent traces or a specific trace
./build/Debug/iee.exe trace
./build/Debug/iee.exe trace --id <trace_id>
```

## API Usage

```powershell
# start local API
./build/Debug/iee.exe api --port 8787
```

Available routes:

- `GET /health`
- `GET /intents`
- `GET /capabilities`
- `POST /execute`
- `POST /explain`

Example execute payload:

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
- adapter reliability scoring behavior
- failure injection and fallback behavior
- API hardening under malformed payloads
- execution stress-loop stability

## Engineering Rules

This repo follows the IEE constitution:

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