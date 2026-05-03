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

## Adapter-Native Continuous Execution Path

Phase 15 continuation work closes the execution gap between coordinated runtime output and adapter execution:

- Intent validation now supports UI-targeted move intents carrying continuous control fields.
- InputAdapter now executes continuous move vectors through native cursor motion on Windows.
- fire/interact flags are mapped to native click signals in the same control pass.

This keeps continuous control additive over existing intent contracts while making coordinated output executable in the main runtime path.

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

## v4.0 Phase 16 Additive Layer

v4.0 extends the existing v3.2.1 coordination stack with deterministic skill intelligence.

New runtime primitives in ReflexCoordination:

- SkillCondition and SkillOutcome for explicit skill state gating.
- SkillNode for hierarchical skill composition with deterministic child ordering.
- AnticipationSignal and AnticipationEvent for short-horizon future-state awareness.
- TemporalStrategyPlan and StrategyMilestone for goal-conditioned multi-step intent planning.
- PreemptionDecision for bounded runtime override decisions.

### Extended decision loop

```text
EnvironmentState -> UniversalReflexAgent::Step
                 -> AttentionMap + PredictedState
                 -> Specialist bundles + Meta bundle
                 -> MicroPlanner refinement
                 -> SkillMemoryStore::RankSkillsForGoal
                 -> SkillMemoryStore::BuildHierarchy
                 -> BuildAnticipationSignal
                 -> BuildTemporalStrategy
                 -> EvaluatePreemption
                 -> ActionCoordinator + ContinuousController
                 -> Intent mapping + telemetry + persistence
```

Fallback behavior remains additive:

- If no ranked skills exist, strategy uses goal-preferred fallback milestones.
- If anticipation is low confidence, strategy remains monitor-only.
- If preemption confidence is below threshold, existing bundle ordering is preserved.
- If any v4.0 signal is empty, v3.2.1 execution path remains intact.

### Runtime observability additions

URE status now includes:

- skill_hierarchy_frames
- anticipation_frames
- strategy_frames
- preempted_frames
- ranked_skills
- skill_hierarchy
- anticipation
- strategy
- preemption

New URE API surfaces:

- GET /ure/skills
- GET /ure/skills/active
- GET /ure/anticipation
- GET /ure/strategy

### Determinism and bounded complexity

- Skill ranking uses stable token overlap and bounded scoring heuristics.
- Hierarchy construction is sorted and size-bounded.
- Anticipation events are capped and confidence-clamped.
- Strategy milestone count is bounded.
- Preemption applies deterministic tie-break ordering by source and priority.
