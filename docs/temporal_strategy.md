# IEE v4.0 Temporal Strategy

## Purpose
Temporal strategy introduces goal-conditioned multi-step planning that remains bounded, deterministic, and additive over existing bundle coordination.

## Core model

### StrategyMilestone
Atomic strategy step:

- skill_name
- target_object_id
- completed

### TemporalStrategyPlan
Frame-level strategy state:

- strategy_id
- goal
- active
- milestones
- confidence
- horizon_frames

### PreemptionDecision
Bounded override signal:

- should_preempt
- reason
- suggested_source
- confidence

## Build flow

BuildTemporalStrategy inputs:

- active goal
- ranked skills
- attention map
- anticipation signal

Build steps:

1. If goal is inactive, return inactive strategy.
2. Derive deterministic strategy_id from normalized goal text.
3. Select bounded milestone list from ranked skills.
4. Fallback to preferred goal action when ranked skills are empty.
5. Blend confidence from skill success and anticipation confidence.
6. Compute bounded horizon_frames.

## Preemption flow

EvaluatePreemption inputs:

- current strategy
- anticipation signal
- reflex decision
- planned bundles

Behavior:

1. If strategy inactive, keep current plan.
2. If no bundles, preempt to meta policy fallback.
3. Find highest-priority bundle with stable tie-break.
4. Compute confidence from priority delta + anticipation boost.
5. Preempt only above deterministic thresholds.

## Runtime integration

When preemption triggers, matching bundle source receives bounded priority boost before final coordination sorting.

This preserves existing bundle architecture while enabling strategy-aware override behavior.

## API surface

- GET /ure/strategy

Response includes:

- strategy payload
- preemption payload

## Safety and fallback

- no strategy hard-failure path
- if strategy is empty, v3.2.1 coordination still executes
- all confidence values are clamped to [0,1]
- bundle ordering remains deterministic
