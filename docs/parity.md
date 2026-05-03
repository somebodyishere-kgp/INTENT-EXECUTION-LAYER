# IEE v3.2.1 Requirement Parity (Phase 15)

## Objective Coverage

| Requirement | Expected | Implemented | Status |
|---|---|---|---|
| Multi-intent bundle model | Runtime should synthesize multiple intent candidates per frame | Added ReflexBundle model and specialist bundle generation | Met |
| Continuous control layer | Stable analog-style control outputs | Added ContinuousAction + ContinuousController smoothing | Met |
| Action coordination engine | Merge/conflict handling across specialist outputs | Added ActionCoordinator with deterministic resolution and conflict filtering | Met |
| Parallel specialist agents | Dedicated movement/aim/interaction/strategy proposals | Added MovementAgent, AimAgent, InteractionAgent, StrategyAgent using parallel tasks | Met |
| Attention system | Surface high-salience focus objects per frame | Added AttentionMap generation and API/status exposure | Met |
| Predictive world model surface | Short-horizon object motion prediction | Added PredictedState generation from temporal centers with confidence scoring | Met |
| Micro-planner | Refine and prioritize bundles before execution mapping | Added MicroPlanner refinement pass and bounded output list | Met |
| Skill memory | Track learned bundle behavior over time | Added SkillMemoryStore record/load/save and status serialization | Met |
| Continuous input execution mapping | Convert coordinated continuous signals into runtime intents | Added BuildIntentFromContinuousAction with vector parameter mapping and InputAdapter native execution support for UI move signals | Met |
| Loop timing constraints | Preserve bounded decision loop timing with diagnostics | Existing decision/loop timing preserved with coordination telemetry | Met |
| API surface extension | Expose bundle, attention, prediction diagnostics | Added GET /ure/bundles, GET /ure/attention, GET /ure/prediction | Met |
| CLI extension | Debug and demo access to new diagnostics | Added iee ure debug --bundles / --continuous and richer demo sampling | Met |
| Richer goal schema parsing | Accept array and bool fields in goal payloads | Added ParseGoalPayload with flat and object/array parsing paths | Met |
| Disk-backed runtime persistence | Goal/experience/skills survive process restart | Added restore/persist hooks and artifact-backed files | Met |
| Mandatory demos | Demonstrate fluid behavior in varied scenarios | Realtime scenarios executed with non-empty coordinated outputs | Met |

## Backward Compatibility

| Legacy Contract | Expectation | Outcome |
|---|---|---|
| Existing v2 routes | Must remain available | Preserved |
| Existing action/task execution contracts | Must remain deterministic and policy-gated | Preserved |
| Existing v3.1 URE routes | Must remain available and additive | Preserved |

## Validation Evidence

- Release build baseline is healthy.
- Integration tests for universal reflex and API hardening include new route checks.
- Realtime demo outputs include bundles, coordinated_output, attention, and prediction data.

## Residual Risks (Non-Blocking)

1. Continuous vector mapping now executes through InputAdapter native cursor/click control; richer adapter-specific analog implementations are still pending.
2. Prediction is short-horizon deterministic extrapolation and not a learned sequence model.
3. Skill memory stores compact bundle-level sequences; deeper hierarchical policy learning is out of scope for v3.2.1.

## v4.0 Phase 16 Requirement Parity

| Requirement | Expected | Implemented | Status |
|---|---|---|---|
| Hierarchical skills | Tree-oriented skill representation with composable nodes | Added SkillNode, SkillCondition, SkillOutcome and hierarchy serialization | Met |
| Goal-aware skill ranking | Deterministic skill selection aligned with active goal | Added SkillMemoryStore::RankSkillsForGoal with bounded token-overlap scoring | Met |
| Skill hierarchy observability | Queryable hierarchy and active skill state | Added GET /ure/skills and GET /ure/skills/active | Met |
| Anticipation engine | Short-horizon future signals with confidence and actionability | Added BuildAnticipationSignal and GET /ure/anticipation | Met |
| Temporal strategy model | Goal-conditioned milestone strategy plan | Added BuildTemporalStrategy and GET /ure/strategy | Met |
| Preemptive control | Deterministic preemption recommendation and runtime override | Added EvaluatePreemption and preemption status payloads | Met |
| Runtime metrics for new layers | Per-frame counters for v4.0 layer activity | Added skill_hierarchy_frames, anticipation_frames, strategy_frames, preempted_frames | Met |
| Backward compatibility | Preserve v3.2.1 runtime behavior when v4.0 signals are absent | Added fallback-safe integration path with unchanged baseline endpoints | Met |
| CLI exposure | Operator-level access to v4.0 state | Added iee ure skills, iee ure anticipation, iee ure strategy | Met |
| Integration validation | Ensure new routes work in hardened and reflex integration suites | Updated integration_api_hardening and integration_universal_reflex | Met |

Residual v4.0 gaps (non-blocking):

1. Hierarchical skill learning is still heuristic and does not yet include long-horizon policy extraction.
2. Anticipation and strategy are deterministic and bounded; no probabilistic ensemble layer is included by design.
3. Adapter-native advanced continuous contracts for non-input adapters remain future additive work.
