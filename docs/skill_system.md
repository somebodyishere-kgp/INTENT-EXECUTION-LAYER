# IEE v4.0 Skill System

## Purpose
The v4.0 skill system upgrades flat execution memories into deterministic, queryable skill intelligence.

It remains additive over v3.2.1 and does not bypass the intent contract.

## Core data model

### Skill
Persisted skill record with runtime execution history and planning metadata.

Fields:

- name
- sequence (Action list)
- attempts
- success_count
- updated_at_ms
- category
- dependencies
- complexity_level
- estimated_frames

### SkillNode
Hierarchical node used for strategy planning and active-skill introspection.

Fields:

- id
- name
- category
- children
- conditions (SkillCondition list)
- last_outcome (SkillOutcome)

### SkillCondition
Simple deterministic condition tuple:

- field
- expected

### SkillOutcome
Most recent node result summary:

- success
- confidence
- note

## Runtime services

### RankSkillsForGoal
Deterministic ranking by:

- success rate
- bounded attempt bonus
- complexity penalty
- estimated frame cost bonus
- goal token overlap bonus

Tie-break ordering remains stable by skill name.

### BuildHierarchy
Constructs a deterministic bounded hierarchy:

- primitive nodes from ranked skills
- optional synthetic strategy_root
- sorted IDs and deduplicated child references

## Persistence

Skill memory remains TSV-based and backward compatible.

v4.0 extends the row format with additional columns while preserving old parsing behavior.

Legacy records continue to load with safe defaults.

## API surfaces

- GET /ure/skills
  - returns ranked_skills and skill_hierarchy
- GET /ure/skills/active
  - returns active strategy ID, active skill names, and preemption snapshot

## Safety and determinism

- bounded list sizes
- stable sorts
- confidence clamping
- thread-safe skill store access via mutex guards
- fallback-safe defaults when no goal or no skills are present
