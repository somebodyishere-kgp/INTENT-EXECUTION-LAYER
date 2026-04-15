# IEE v3.2.1 Continuous Control

## Purpose

This document defines how Phase 15 maps coordinated reflex output into continuous runtime control signals.

The goal is to keep control fluid and low-latency while preserving deterministic bounds and existing queue-based execution contracts.

## Continuous Signal Model

Continuous control uses this signal structure:

- move_x, move_y
- aim_dx, aim_dy
- look_dx, look_dy
- fire
- interact

Value bounds:

- all float axes are clamped to [-1.0, 1.0]
- booleans are strict true/false

## Smoothing Model

ContinuousController applies bounded smoothing per axis.

For each axis:

next = clamp(previous + (target - previous) * alpha)

Where:

- alpha is smoothingAlpha in [0.05, 1.0]
- clamp enforces unit bounds
- sensitivity scales the raw target before smoothing

This avoids abrupt oscillations while still allowing fast response.

## Runtime Flow

1. Specialist and meta-policy bundles produce raw continuous candidates.
2. ActionCoordinator fuses weighted outputs into one coordinated continuous vector.
3. ContinuousController smooths the vector.
4. Runtime maps final vector into intent params for queue execution.

This keeps control deterministic even when multiple specialists contribute simultaneously.

## Intent Mapping Contract

Continuous output is mapped to one runtime intent per decision pass when meaningful:

- action type: move
- source: ure-runtime
- bundle source hint: continuous_controller
- params include all axis and boolean fields

A continuous intent is emitted only when vector content exceeds minimal thresholds or boolean toggles are active.

## Diagnostics and API Visibility

Continuous outputs are visible in:

- GET /ure/status (coordinated_output)
- GET /ure/bundles (coordinated_output)
- POST /ure/step response
- POST /ure/demo response

Related runtime counters:

- bundle_frames
- coordinated_actions

## Safety and Determinism

Continuous control obeys existing safety constraints:

- policy gating remains authoritative
- no direct adapter bypass
- bounded values and deterministic merge/sort behavior
- non-blocking queue dispatch remains unchanged

## Current Scope and Limitations

- Continuous vectors are currently transported through generic intent params.
- Adapter-specific native analog bindings are future extension points.
- Prediction and smoothing remain deterministic heuristics by design in v3.2.1.
