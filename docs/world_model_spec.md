# World Model Specification (IEE v3.0)

## Purpose
Provide a lightweight, deterministic, frame-level structural representation of the environment for reflex decisions.

## Core Entities

### WorldObject
Fields:

- `id`
- `type`
- `bbox`
- `label`
- `actionable`
- `dynamic`
- `salience`
- `lastSeenFrame`
- `affordances`

### Relationship
Fields:

- `fromId`
- `toId`
- `type` in `{ proximity, overlap, motion, hierarchy }`
- `strength`

### WorldModel
Fields:

- `frameId`
- `signature`
- `objects`
- `relationships`
- `changedObjects`

## Update Contract

Per frame:

1. Extract universal features.
2. Build/update objects by stable feature ids.
3. Compare with previous world model for temporal continuity.
4. Mark changes and motion.
5. Recompute bounded relationship graph.
6. Emit deterministic signature.

## Temporal Consistency Rules

- Object identity is stable by deterministic feature ids.
- Motion is inferred from center displacement over threshold.
- Change detection compares type, salience, and position drift.

## Relationship Semantics

- `proximity`: spatial closeness under bounded distance.
- `overlap`: area intersection ratio > 0.
- `motion`: same object moved across frames.
- `hierarchy`: geometric containment relation.

## Complexity and Bounds

To remain reflex-fast:

- relationship generation is bounded to top object window (fixed max)
- deterministic sorting is applied before pairwise relation generation
- all loops are bounded and non-blocking

## Serialization

World model JSON includes:

- frame metadata
- ordered object list
- ordered relationship list
- change counter

This payload is consumed by:

- `GET /ure/world-model`
- `POST /ure/step`
- `POST /ure/demo`
