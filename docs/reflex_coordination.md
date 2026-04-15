# IEE v3.2.1 Reflex Coordination

## Purpose

This document specifies the coordination layer introduced in Phase 15 for composing multi-intent reflex behavior in the continuous URE runtime.

The coordination layer sits between reflex step output and runtime intent emission.

## Core Data Structures

Defined in core/reflex/include/ReflexCoordination.h:

- ReflexBundle
- ContinuousAction
- CoordinatedOutput
- AttentionMap
- PredictedState
- Skill

These types provide stable contracts for specialist proposals, fusion, diagnostics, and persistence.

## Specialist Agents

The runtime uses four specialist proposal agents:

- MovementAgent
- AimAgent
- InteractionAgent
- StrategyAgent

Each agent reads world model + attention + goal + safety and returns a bounded ReflexBundle.

Specialists are evaluated in parallel and then merged with a meta-policy bundle derived from the reflex decision.

## Attention and Prediction

Before coordination:

- BuildAttentionMap selects highest-salience focus objects.
- BuildPredictedStates estimates short-horizon future positions from temporal center shifts.

These outputs are published to runtime diagnostics and consumed by bundle logic.

## MicroPlanner Refinement

MicroPlanner refines bundle priorities and trims low-value bundles.

Current behavior:

- boosts interaction and target-aligned bundles under active goals
- enforces deterministic ordering
- caps bundle count to keep latency bounded

## ActionCoordinator Fusion

ActionCoordinator resolves final output in two channels:

- Continuous channel: weighted fusion by bundle priority
- Discrete channel: conflict-aware action selection

Conflict handling rules include:

- avoid duplicate intent actions
- reject opposite directional movement pairs
- preserve deterministic tie breaks by sorted bundle order

## ContinuousController

After fusion, ContinuousController applies smoothing and sensitivity scaling:

- clamps all axes to unit range
- smooths transitions across frames
- preserves boolean triggers (fire/interact)

## Skill Memory

SkillMemoryStore tracks bundle-level action sequences with attempts and success counts.

Persistence file:

- artifacts/reflex/skills_v3_2.tsv

Runtime updates are written through execution observer feedback and serialized on persistence events.

## API Surface

Coordination diagnostics are exposed through:

- GET /ure/bundles
- GET /ure/attention
- GET /ure/prediction
- GET /ure/status
- POST /ure/step
- POST /ure/demo

These payloads include bundles, coordinated_output, attention focus, prediction vectors, and skill summaries.

## Determinism Guarantees

The coordination layer keeps deterministic behavior by:

- stable ordering on equal priorities
- bounded list sizes for bundles and predictions
- fixed conflict resolution rules
- no stochastic inference steps

This preserves IEE architecture goals: additive integration, safety gating, and predictable runtime control.
