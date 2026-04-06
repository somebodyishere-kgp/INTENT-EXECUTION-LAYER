# IEE v1.2 Architecture

## Purpose
IEE v1.2 upgrades v1.1 into a real-time control runtime while preserving deterministic intent execution and v1.1 public interfaces. The runtime now supports long-running control loops, stronger cache invalidation, persisted telemetry, and control-plane HTTP endpoints.

## Runtime Modules

### core/observer
- Captures active window/process/cursor context and filesystem snapshot.
- Emits monotonic snapshot sequence IDs used by control/runtime cache proofs.

### core/accessibility
- Native UIA primitives (`Activate`, `SetValue`, `Select`) remain first-class.
- Continues supplying element metadata (depth/focus/position) for deterministic resolution.

### core/capability
- Builds capability graph nodes/edges from observer snapshots.
- Provides stable structure for adapter intent emission.

### core/intent
- `Context` now carries runtime proof metadata:
  - `snapshotTicks`
  - `snapshotVersion`
  - `controlFrame`
- Registry cache invalidation upgraded to v2:
  - cache key = action + normalized target + snapshot sequence + cache epoch
  - cache epoch increments on refresh and interaction writes

### core/execution
- `ExecutionEngine` retains `Execute(const Intent&)` and adds budgeted execution path:
  - `ExecuteWithBudget(const Intent&, std::chrono::milliseconds)`
- Timeout handling now enforces both per-attempt and cumulative retry budget windows.
- Fast-path cache upgraded to v2:
  - key includes action/target/targetType + parameter hash
  - entry includes snapshot ticks + snapshot version
  - LRU-style eviction based on last access
- Adapter set now includes:
  - `UIAAdapter`
  - `InputAdapter` (deterministic keyboard/mouse simulation baseline)
  - `FileSystemAdapter`

### core/execution Control Runtime (new)
- New `ControlRuntime` provides continuous control loop execution:
  - configurable frame budget (`targetFrameMs`)
  - optional `maxFrames` cap
  - high/medium/low intent queues
  - event-priority-aware refresh handling
  - immutable runtime snapshots with versioned loop state
- Supports enqueue-based intent dispatch for real-time API flows.

### core/telemetry
- Existing in-memory traces/metrics retained.
- New persistence pipeline:
  - async queue ingestion
  - rotating JSONL files under `artifacts/telemetry`
  - bounded queue and bounded rotated file set
- Snapshot output now includes persistence health and buffer-wrap indicators.

### core/event
- Event bus priorities continue to classify reactive urgency:
  - `HIGH`, `MEDIUM`, `LOW`
- Control runtime consumes these priorities to schedule refresh work in cycle order.

### interface/cli
- Existing commands preserved.
- Observability extensions:
  - telemetry filtering (`--status`, `--adapter`, `--limit`)
  - persistence inspection (`telemetry --persistence`)

### interface/api
- Existing routes preserved.
- New v1.2 routes:
  - `GET /control/status`
  - `POST /control/start`
  - `POST /control/stop`
  - `GET /telemetry/persistence`
- `POST /execute` now supports queued runtime mode:
  - `"mode":"queued" | "realtime"`
  - optional `"priority":"high|medium|low"`

## Core Flow (v1.2)
1. Observe and refresh capability/intent state.
2. Resolve deterministic intent candidates with cache-epoch-safe invalidation.
3. Select best adapter via reliability/confidence/latency score.
4. Execute in immediate mode or queue into control runtime.
5. Enforce latency/cumulative timeout constraints.
6. Emit telemetry trace and persist asynchronously.
7. Expose runtime and persistence status through CLI/API.

## Validation Baseline
- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Debug`
- Tests: `ctest --test-dir build -C Debug --output-on-failure`
