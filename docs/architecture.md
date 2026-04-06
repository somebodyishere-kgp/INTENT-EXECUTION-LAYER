# IEE v1.1 Architecture

## Purpose
IEE v1.1 evolves v1 into a real-time, observable, adapter-extensible execution substrate while preserving the same core architecture boundaries:
- deterministic intent resolution and execution
- native adapter execution paths
- strict schema and verification gates
- event-driven refresh and low-latency fast paths

## Runtime Modules

### core/observer
- Captures active window/process/cursor context and filesystem snapshot.
- Emits monotonic snapshot sequence IDs for cache-safe context matching.

### core/accessibility
- UIA capture and execution primitives (`Activate`, `SetValue`, `Select`).
- Provides element depth/focus/location metadata for resolver scoring.

### core/capability
- Builds and maintains capability graph nodes and relations.
- Used as the structural source for intent generation.

### core/intent
- Schema v2 (`Target`, `Params`, `Context`, `Constraints`) and validator.
- Deterministic resolver ranking by label/depth/proximity/focus/recency.
- Registry now includes a real-time resolution cache keyed by:
  - action
  - normalized target
  - snapshot sequence
- Registry records intent-resolution timing into telemetry.

### core/execution
- Adapter SDK surface:
  - `Name()`
  - `GetCapabilities(...)`
  - `Execute(...)`
  - `GetScore()`
  - `Subscribe(...)`
- SDK-form aliases are also available (`name`, `getCapabilities`, `execute`, `getScore`, `subscribe`) for external adapter consistency.
- `AdapterRegistry` now performs score-based resolution using runtime-updated metrics.

Adapter score model:
- `AdapterScore { reliability, latency, confidence }`
- rolling reliability and latency EMA updates after each execution
- exponential decay applied to stale runtime samples
- deterministic tie-break on registration order
- selection score:
  - `score = 0.55*reliability + 0.35*confidence - 0.10*latency_penalty`

Execution engine v1.1 additions:
- trace ID per execution
- telemetry logging for adapter decision and execution outcome
- timeout gate (`constraints.timeoutMs`) enforced after adapter call
- fast-path adapter cache keyed by action/target/context tick for sub-100ms repeat cycles

### core/telemetry (new)
- In-memory execution trace and metrics subsystem.
- Tracks:
  - execution traces (trace id, intent, target, adapter, duration, status)
  - per-adapter success rate and average latency
  - average intent resolution time
- Provides JSON serialization used by CLI and API diagnostics.

### core/event
- Event bus now carries `EventPriority`:
  - `HIGH`
  - `MEDIUM`
  - `LOW`
- Watchers publish priority-aware events:
  - focus/foreground change -> `HIGH`
  - name change -> `MEDIUM`
  - filesystem change -> `LOW`

### interface/cli
- Existing commands preserved.
- New observability commands:
  - `telemetry`
  - `trace <trace_id>`

### interface/api
- Hardened local HTTP API with strict top-level JSON validation and structured errors.
- Routes:
  - `GET /health`
  - `GET /intents`
  - `GET /capabilities`
  - `POST /execute`
  - `POST /explain`
- Runtime protections:
  - payload size limit
  - socket read timeout
  - bounded concurrent request handling in long-running mode

## Core Flow (v1.1)
1. Observe state and update registry (full or incremental).
2. Resolve intent with cache-aware deterministic ranking.
3. Select best adapter via reliability/confidence/latency scoring.
4. Execute with retry/fallback/timeout gates.
5. Verify and return structured status.
6. Emit telemetry trace + adapter metrics + event signals.

## Real-Time Readiness Controls
- incremental UI/filesystem refresh paths
- priority-tagged events
- fast-path adapter reuse when context is unchanged
- bounded cache sizes and deterministic eviction behavior

## Validation Baseline
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`
