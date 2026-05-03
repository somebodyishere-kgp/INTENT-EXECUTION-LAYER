# IEE v4.0 Anticipation Engine

## Purpose
The anticipation engine adds deterministic short-horizon forecasting so URE can bias coordination before state changes fully materialize.

## Core model

### AnticipationEvent
Per-object future prediction summary:

- object_id
- future_position
- confidence

### AnticipationSignal
Frame-level anticipation payload:

- horizon_frames
- events
- anticipated_affordances
- drift_confidence
- actionable
- reason

## Build flow

BuildAnticipationSignal inputs:

- current WorldModel
- current AttentionMap
- current PredictedState list
- horizon_frames

Build steps:

1. Copy top bounded predicted events.
2. Populate anticipated affordances from attention objects.
3. Fallback to bounded object IDs if attention is empty.
4. Compute drift_confidence from top prediction and attention weighting.
5. Mark actionable only when confidence passes threshold.

## Runtime use

AnticipationSignal is consumed by:

- temporal strategy synthesis (confidence blending)
- preemption evaluation (anticipation boost)
- runtime observability APIs and status payloads

## API surface

- GET /ure/anticipation

Response includes:

- horizon_frames
- events
- anticipated_affordances
- drift_confidence
- actionable
- reason

## Performance constraints

- bounded event list
- bounded affordance list
- no external ML dependency
- deterministic calculations and confidence clamping
