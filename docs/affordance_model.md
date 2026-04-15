# Affordance Model (IEE v3.0)

## Goal
Map structural world-object types to deterministic action possibilities.

The mapping is domain-agnostic and does not rely on app-specific templates.

## Type-to-Affordance Rules

| Object Type | Affordances |
|---|---|
| `interactive_object` | `click`, `activate` |
| `control_surface` | `drag`, `adjust` |
| `dynamic_object` | `track`, `target` |
| `text_region` | `read`, `input` |
| `target` | `read`, `input` |
| `navigation_element` | `click`, `navigate` |
| `resource` | `acquire`, `activate` |
| `obstacle` | `avoid` |
| `unknown` | `explore` |

## Determinism Rules

1. Actions are sorted and deduplicated.
2. Confidence is bounded to `[0,1]`.
3. Tie-break ordering is stable (`object_id` ascending).
4. No stochastic choice in action list generation.

## Confidence Model

Base confidence is derived from object salience.

Adjustment factors:

- positive reward history increases confidence bias
- repeated failures decrease confidence bias

Confidence adjustment is bounded and local to `(object_type, action)`.

## Safety Constraints

Affordance generation itself is pure and side-effect free.

Execution safety is enforced downstream by policy checks and action-contract gating.

## Extension Guidelines

When adding new affordance primitives:

1. Keep names generic and reusable.
2. Add deterministic mapping rule.
3. Add non-breaking serialization support.
4. Add route/test coverage.
