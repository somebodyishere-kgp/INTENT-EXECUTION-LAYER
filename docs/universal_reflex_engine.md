# Universal Reflex Engine (URE)

## Overview
The Universal Reflex Engine is IEE v3.0's real-time, structure-driven intelligence layer.

URE operates without per-step LLM calls and without heavy training loops in core runtime.

## Processing Loop

```text
observe -> extract -> model -> afford -> decide -> (optional) act
```

## Core Contracts

### UniversalFeature
- `type`: domain-agnostic classification
- `bbox`: spatial footprint
- `actionable`: execution candidate flag
- `dynamic`: temporal movement flag
- `salience`: relative importance score

### WorldModel
- tracked `objects`
- `relationships` (proximity, overlap, motion, hierarchy)
- frame signature + change count

### Affordance
- object-to-actions mapping
- deterministic action primitives

### ReflexDecision
- selected object + action + reason + priority
- executable flag
- exploratory flag

### ExperienceEntry
- compact world-state summary
- executed decision
- reward
- timestamp

## Safety and Execution

URE does not bypass core execution contracts.

- Optional execution path goes through `ActionExecutor`.
- Policy gate (`PermissionPolicyStore`) is checked before execution.
- Exploration remains bounded and policy-aware.

## API Surfaces

- `GET /ure/world-model`
- `GET /ure/affordances`
- `GET /ure/decision`
- `GET /ure/metrics`
- `GET /ure/experience`
- `POST /ure/step`
- `POST /ure/demo`

## Performance Signals

Per step:

- `decision_time_us`
- `loop_time_us`
- `decision_within_budget`

Aggregates:

- average decision ms
- p95 decision ms
- average loop ms
- over-budget count
- exploratory decision count

## Adaptation Model

URE adapts through interaction outcomes, not offline training.

- successes reduce local failure bias
- failures increase local failure bias
- affordance confidence is adjusted by outcome

This keeps adaptation fast, bounded, and deterministic.
