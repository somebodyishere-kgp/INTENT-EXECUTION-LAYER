# Intent Execution Engine (IEE)

IEE is a native C++ system runtime that converts live OS/application state into a deterministic, queryable, and executable intent layer.

It is built as an execution substrate and control plane, not a UI product.

## Why IEE

Conventional automation stacks fail under ambiguity, stale state, weak observability, and execution drift.
IEE addresses this with:

- deterministic intent resolution and execution
- bounded fallback and recovery behavior
- explicit policy and safety controls
- verifiable execution contracts
- telemetry-first runtime diagnostics

## Runtime Stage

This repository includes:

- IEE v1.x to v2.x foundations (observer, capability graph, interaction graph, action interface, policy, workflows)
- IEE v3.0 (Phase 13.5): Universal Reflex Engine (URE)
- IEE v3.1 (Phase 14): continuous URE integration into the control runtime loop

## IEE v3.1 Highlights (Continuous URE Runtime)

1. Continuous real-time reflex loop
- Added a continuous URE decision provider integrated with `ControlRuntime`.
- URE now evaluates synchronized environment frames continuously while runtime is active.

2. Runtime control endpoints
- Added continuous URE control routes:
        - `POST /ure/start`
        - `POST /ure/stop`
        - `GET /ure/status`
        - `POST /ure/goal`
        - `GET /ure/goal`

3. Goal-conditioned reflex behavior
- Added `ReflexGoal` model and goal-aware policy decisioning.
- Reflex priority and chosen actions can be conditioned by active goal + preferred actions.

4. Priority-aware non-blocking action pipeline
- Added queue-priority hints from URE decision provider into control runtime intent scheduling.
- Reflex actions are still executed through existing execution contracts and policy gates.

5. Real-time feedback adaptation
- Added runtime execution observer callback integration.
- Reflex experience memory is updated from actual action outcomes in continuous mode.

6. Telemetry merge
- Added reflex telemetry stream and merged reflex summary into main telemetry snapshot.
- Added `GET /telemetry/reflex` route.

7. CLI runtime control
- Added CLI command group:
        - `iee ure live`
        - `iee ure debug`
        - `iee ure demo realtime`

## IEE v3.0 Highlights (Universal Reflex Engine Base)

1. Universal feature extraction
- Added structural feature extraction from UIG, ScreenState, and cursor motion.
- Feature types include: `interactive_object`, `dynamic_object`, `control_surface`, `target`, `obstacle`, `resource`, `navigation_element`, `text_region`.

2. Real-time world model
- Added per-frame world model building with temporal consistency.
- Object and relationship tracking includes: proximity, overlap, hierarchy, and motion.

3. Affordance inference
- Added deterministic type-to-affordance mapping without app-specific logic.
- Affordances are generated as reusable action primitives.

4. Meta-policy reflex decisions
- Added priority-based universal policy rules:
  - threat -> reduce risk
  - target -> move toward
  - resource -> acquire
  - obstacle -> avoid
  - unknown -> explore

5. Bounded exploration and experience memory
- Added safe exploration proposals for unknown states.
- Added reward-based experience memory and failure-bias adaptation to avoid repeated mistakes.

6. Reflex API integration
- Added URE routes:
  - `GET /ure/world-model`
  - `GET /ure/affordances`
  - `GET /ure/decision`
  - `GET /ure/metrics`
  - `GET /ure/experience`
  - `POST /ure/step`
  - `POST /ure/demo`

7. Safety and policy integration
- Reflex execution obeys `PermissionPolicyStore`.
- Exploration is gated by policy, deterministic, and bounded.

8. Performance model
- Reflex step tracks microsecond decision/loop timing.
- Metrics expose average and p95 decision time.

## Architecture

```text
EnvironmentState (ScreenState + UIG)
        |
        v
UniversalFeatureExtractor
        |
        v
WorldModelBuilder
        |
        v
AffordanceEngine
        |
        v
MetaPolicyEngine
        |
        v
UniversalReflexAgent + UreDecisionProvider
        |
        +--> ControlRuntime queue (priority-aware)
        |       |
        |       +--> ExecutionEngine -> Action adapters
        |       +--> Execution observer -> Reflex outcome update
        |
        +--> (optional step execute) ActionExecutor -> /act contract
        |
        +--> ExplorationEngine + ExperienceMemory
        |
        +--> Reflex telemetry + API surfaces
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
  reflex/
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
# core runtime commands
./build/Release/iee.exe list-intents
./build/Release/iee.exe state/ai --pure-json
./build/Release/iee.exe act "open command palette" --json

# telemetry and contracts
./build/Release/iee.exe telemetry --json
./build/Release/iee.exe perf --json --strict
./build/Release/iee.exe vision --json --limit 300

# run local API
./build/Release/iee.exe api --port 8787

# continuous URE runtime
./build/Release/iee.exe ure live --samples 20 --interval_ms 120
./build/Release/iee.exe ure debug --json
./build/Release/iee.exe ure demo realtime --goal "stabilize active interaction target"
```

## API Quickstart

```powershell
./build/Release/iee.exe api --port 8787
```

Key routes:

- `GET /health`
- `GET /state/ai`
- `POST /act`
- `POST /task/plan`
- `POST /task/semantic`
- `GET /interaction-graph`
- `GET /trace/{trace_id}`
- `GET /policy`
- `POST /policy`
- `GET /perf`
- `GET /perf/percentiles`
- `GET /perf/frame-consistency`
- `GET /ure/world-model`
- `GET /ure/affordances`
- `GET /ure/decision`
- `GET /ure/metrics`
- `GET /ure/experience`
- `GET /ure/status`
- `GET /ure/goal`
- `GET /telemetry/reflex`
- `POST /ure/step`
- `POST /ure/demo`
- `POST /ure/start`
- `POST /ure/stop`
- `POST /ure/goal`

## URE Example Request

```json
{
  "execute": "true",
  "decision_budget_us": "1000"
}
```

`POST /ure/step` returns world model, affordances, reflex decision, timing, and optional action execution result.

## Documentation

Primary docs:

- [docs/architecture.md](docs/architecture.md)
- [docs/status.md](docs/status.md)
- [docs/parity.md](docs/parity.md)
- [docs/issues_and_errors.md](docs/issues_and_errors.md)
- [docs/context_handoff.md](docs/context_handoff.md)
- [docs/universal_reflex_engine.md](docs/universal_reflex_engine.md)
- [docs/affordance_model.md](docs/affordance_model.md)
- [docs/world_model_spec.md](docs/world_model_spec.md)
- [docs/reflex_runtime.md](docs/reflex_runtime.md)

## License

See [LICENSE](LICENSE).
