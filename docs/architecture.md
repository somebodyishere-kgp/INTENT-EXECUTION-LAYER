# IEE v3.2.1 Architecture

## Purpose
IEE v3.2.1 upgrades the Universal Reflex Engine (URE) runtime from single-intent reflex output to fluid multi-intent continuous intelligence.

The design remains additive over v3.1 and preserves existing v2/v3 execution and safety contracts.

## Phase 15 Continuous Pipeline

```text
EnvironmentState (ScreenState + UIG)
        |
        v
UniversalReflexAgent::Step
        |
        +--> AttentionMap builder
        +--> PredictedState builder
        |
        +--> Specialist bundle agents (parallel)
        |       |- MovementAgent
        |       |- AimAgent
        |       |- InteractionAgent
        |       '- StrategyAgent
        |
        +--> MicroPlanner (bundle refinement)
        +--> ActionCoordinator (conflict-aware fusion)
        +--> ContinuousController (signal smoothing)
        |
        +--> Intent mapping
        |       |- coordinated discrete actions -> intents
        |       '- coordinated continuous vector -> intent params
        |
        +--> ControlRuntime queue (priority-aware, non-blocking)
        +--> ExecutionObserver feedback -> Reflex + Skill memory updates
        +--> Telemetry + runtime status cache
        +--> Disk persistence (goal, experience, skills)
```

## New Coordination Module

Added:

- core/reflex/include/ReflexCoordination.h
- core/reflex/src/ReflexCoordination.cpp

Key primitives:

- ContinuousAction
- ReflexBundle
- CoordinatedOutput
- AttentionMap
- PredictedState
- Skill

Key services:

- ContinuousController
- ActionCoordinator
- MovementAgent / AimAgent / InteractionAgent / StrategyAgent
- MicroPlanner
- SkillMemoryStore

## Runtime Integration Changes

Phase 15 expands UreDecisionProvider behavior:

- Generates specialist bundles in parallel and appends meta-policy bundle.
- Refines bundles through MicroPlanner before execution mapping.
- Resolves conflicts and fuses signals via ActionCoordinator.
- Smooths final continuous vector through ContinuousController.
- Produces multi-intent outputs per decision pass when execution is enabled.
- Publishes attention, prediction, bundles, and coordinated output to runtime status.

## Goal and Persistence Model

Goal payload parsing now supports richer schema forms:

- flat fields (v3.1 compatible)
- array-based preferred_actions
- bool fields like active/clear in object payloads

Runtime persistence is now disk-backed:

- artifacts/reflex/goal_state_v3_2.json
- artifacts/reflex/experience_state_v3_2.tsv
- artifacts/reflex/skills_v3_2.tsv

Restore path runs during API server startup; save path runs on goal update and runtime stop.

## API and CLI Surface Additions

New API endpoints:

- GET /ure/bundles
- GET /ure/attention
- GET /ure/prediction

Extended payloads:

- POST /ure/step returns attention, prediction, bundles, coordinated_output.
- POST /ure/demo returns attention, prediction, bundles, coordinated_output.
- GET /ure/status now includes bundle counters, coordinated output, and learned skills.

CLI updates:

- iee ure debug --bundles
- iee ure debug --continuous
- iee ure demo realtime now captures per-sample demo payload in JSON mode.

## Determinism and Safety

Phase 15 keeps deterministic and bounded behavior:

- stable sort and tie-break ordering by priority/source/object id
- bounded bundle set after planning
- bounded prediction and attention result counts
- policy-aware execution preserved through existing runtime contracts
- no external ML loop or LLM dependency in reflex path

## Performance and Observability

The runtime continues to expose microsecond timings and budget compliance while adding coordination diagnostics:

- bundle_frames
- coordinated_actions
- attention and prediction snapshots
- coordinated output signals
- skill-memory summaries

Telemetry remains merged through existing reflex telemetry surfaces.

## Validation Baseline

- Build: cmake --build build --config Release
- Test: ctest --test-dir build -C Release --output-on-failure

Integration coverage now includes bundles/attention/prediction endpoints and richer goal payload schema.
