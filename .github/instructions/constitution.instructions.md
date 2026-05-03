description: Load these instructions whenever the agent is working on the Intent Execution Engine (IEE), including designing architecture, writing or reviewing C++ code, managing the repository, debugging, or updating project documentation.

# applyTo: '**/INTENT-EXECUTION-LAYER/**, **/*.cpp, **/*.h, **/docs/**'

---

# Intent Execution Engine (IEE) — Engineering Constitution

Provide project context and coding guidelines that AI should follow when generating code, answering questions, or reviewing changes.

---

## 1. Project Context

The Intent Execution Engine (IEE) is a **system-level runtime** that converts any operating system and application into a **structured, queryable, and executable intent space**.

IEE is NOT:

* an assistant
* an automation script
* a UI tool

IEE IS:

> A universal control plane that enables deterministic execution of software through structured intents.

---

# 2. Core Engineering Principles

## 2.1 System Over Feature

Build reusable systems, not one-off features.

## 2.2 Determinism Over Heuristics

Prefer structured, verifiable logic over guesswork.

## 2.3 Native First Execution

Priority:

1. Native APIs
2. OS APIs
3. Accessibility
4. Input simulation

## 2.4 Event-Driven Design

Avoid polling. Use event-driven systems.

## 2.5 Performance Awareness

Optimize for latency, memory, and CPU.

---

# 3. Language & Tech Constraints

* C++ (C++17/20 only)
* Win32 API, COM, UI Automation
* Avoid external languages in core

---

# 4. Codebase Structure

```
/core
  /observer
  /accessibility
  /capability
  /intent
  /execution
  /event
/interface
  /cli
/docs
```

Strict modular boundaries. No circular dependencies.

---

# 5. Advanced Coding Standards (MANDATORY)

## 5.1 Naming Conventions

### Classes

```cpp
class IntentRegistry;
class CapabilityExtractor;
```

### Functions

```cpp
void extractCapabilities();
bool executeIntent(const Intent& intent);
```

### Variables

```cpp
int activeWindowId;
std::string elementLabel;
```

---

## 5.2 Function Design

Bad:

```cpp
void process() {
    // does everything
}
```

Good:

```cpp
void extractUIElements();
void mapElementsToCapabilities();
void normalizeToIntents();
```

---

## 5.3 Avoid God Objects

Bad:

```cpp
class Engine {
    // observer + execution + parsing + everything
};
```

Good:

* Separate modules with clear responsibility

---

## 5.4 Memory Management

Use RAII:

```cpp
std::unique_ptr<Intent> intent = std::make_unique<Intent>();
```

Avoid raw pointers unless necessary.

---

## 5.5 Error Handling

Bad:

```cpp
if (!result) return;
```

Good:

```cpp
if (!result) {
    logError("Execution failed: invalid state");
    return ExecutionStatus::FAILED;
}
```

---

## 5.6 Logging Standard

```cpp
logInfo("Intent executed: set_background");
logWarning("Multiple matching targets found");
logError("UIA element not accessible");
```

---

# 6. COMMENTING SYSTEM (VERY IMPORTANT)

Comments must explain:

* WHY something exists
* WHAT the logic does (only if complex)
* NEVER obvious code

---

## 6.1 Function Comments

```cpp
/**
 * Extracts actionable UI elements from the accessibility tree
 * and converts them into internal capability representations.
 *
 * This is the first step in building the intent graph.
 */
void extractCapabilities();
```

---

## 6.2 Inline Comments

Use ONLY when needed:

```cpp
// Mapping UIA control type to intent type
if (element.type == UIA_Button) {
    intent.name = "activate";
}
```

---

## 6.3 Avoid Useless Comments

Bad:

```cpp
// increment i
i++;
```

---

## 6.4 Architecture Comments

At top of critical files:

```cpp
/*
 * Module: Capability Extractor
 * Role:
 * Converts raw UI and OS state into structured capabilities.
 *
 * This module is part of the pipeline:
 * Observer → Capability → Intent → Execution
 */
```

---

# 7. Intent System Rules

* All actions MUST go through intent schema
* No direct execution bypass
* Always validate against context
* Always verify execution

---

# 8. Repository Rules

Repo:
https://github.com/somebodyishere-kgp/INTENT-EXECUTION-LAYER.git

---

## 8.1 Mandatory Documentation Updates

After EVERY iteration, update:

### architecture.md

* Updated system design

### status.md

* Progress tracking

### parity.md

* Expected vs actual system

### issues_and_errors.md

* Bugs, root cause, fixes

---

## 8.2 A CRITICAL FILE — context_handoff.md

This file acts as:

> **The memory of the project**

It MUST always contain:

### 1. What are we building

* Clear summary of IEE

### 2. Current state

* What is completed
* What is partially built

### 3. Last work done

* Exact last implemented feature/module

### 4. Current problem

* What blocker exists

### 5. Next plan

* What should be built next (step-by-step)

### 6. Key decisions taken

* Important architectural choices

---

### Example Entry:

```
Last Worked On:
- Implemented UIA tree extraction

Current Issue:
- UI elements not mapping correctly to capabilities

Root Cause:
- Missing role normalization

Next Step:
- Build mapping layer for UIA roles → intent schema
```

---

## RULE:

If context_handoff.md is not updated → development continuity is broken.

---

## 8.3 Commit Rules

Every commit must:

* Be atomic
* Explain WHAT + WHY

---

# 9. Development Workflow

1. Observe
2. Extract
3. Normalize
4. Register
5. Execute
6. Verify

---

# 10. Problem Solving Protocol

* Never patch blindly
* Always identify root cause
* Document before fixing

---

# 11. Restrictions

Do NOT:

* Build UI early
* Use vision systems
* Add unnecessary dependencies
* Skip documentation

---
## 12. Multi-Agent Orchestration Protocol (CRITICAL)

The agent must not operate as a single monolithic entity.

To achieve higher quality, parallelism, and specialization, the system MUST adopt a **multi-agent architecture** where subtasks are delegated to specialized internal agents.

---

### 12.1 Principle

> Complex system-building tasks must be decomposed into smaller, well-defined responsibilities handled by specialized agents.

---

### 12.2 When to Spawn Sub-Agents

The agent MUST spawn sub-agents when:

* Working on independent modules (e.g., observer, execution, intent)
* Performing parallelizable tasks
* Handling complex debugging or root cause analysis
* Designing architecture vs implementing code
* Writing documentation vs writing core logic
* Refactoring large components

---

### 12.3 Types of Sub-Agents

The following specialized roles should be instantiated when needed:

#### 1. Architecture Agent

* Designs system structure
* Defines module boundaries
* Ensures long-term scalability

---

#### 2. Core Implementation Agent

* Writes production-grade C++ code
* Implements modules based on architecture

---

#### 3. Debugging Agent

* Identifies root causes
* Traces failures
* Proposes structured fixes

---

#### 4. Documentation Agent

* Updates:

  * architecture.md
  * status.md
  * parity.md
  * issues_and_errors.md
  * context_handoff.md

Ensures clarity and continuity.

---

#### 5. Refactoring Agent

* Improves code quality
* Removes technical debt
* Enforces modularity

---

### 12.4 Coordination Rules

* A **primary agent** must orchestrate all sub-agents
* Sub-agents must:

  * work independently
  * return structured outputs
* Final integration must be validated before merging

---

### 12.5 Output Integration

All sub-agent outputs must be:

* reviewed
* validated against architecture
* merged cleanly into the codebase

---

### 12.6 Documentation Synchronization

After multi-agent execution:

* Documentation Agent MUST update all required docs
* context_handoff.md MUST reflect:

  * which agents were used
  * what each contributed
  * current system state

---

### 12.7 Constraint

Sub-agent usage must:

* improve clarity
* reduce complexity
* increase correctness

If it introduces chaos → it must be avoided.

---

### FINAL RULE

> Do not think as a single coder.
> Operate as a coordinated system of specialized engineering agents.

---

# 13. Final Directive

You are not writing code.

You are building:

> **A universal execution layer for all software**

Every line must improve:

* clarity
* modularity
* scalability
* determinism

---
## 14. Repository Enforcement Protocol (CRITICAL — NON-NEGOTIABLE)

The agent MUST actively manage and maintain the GitHub repository:

https://github.com/somebodyishere-kgp/INTENT-EXECUTION-LAYER.git

This is not optional. Writing code locally without properly maintaining the repository is considered **failure of execution**.

---

### 14.1 Mandatory Git Workflow

After EVERY meaningful change, the agent MUST:

1. Stage changes
2. Commit with proper message
3. Push to repository

---

### 14.2 Commit Standards

Every commit MUST:

* Be atomic (single logical change)
* Follow format:

```id="o7axd1"
<type>: <short description>

<detailed explanation of what and why>
```

### Types:

* feat → new system/module
* fix → bug fix
* refactor → structural improvement
* docs → documentation updates
* test → testing additions

---

### Example:

```id="0g3o7w"
feat: add intent resolution engine

Implemented ranking-based disambiguation for multiple matching UI targets
using proximity, hierarchy, and focus signals.
```

---

### 14.3 Documentation Sync Rule

Before EVERY commit, ensure ALL required docs are updated:

* architecture.md
* status.md
* parity.md
* issues_and_errors.md
* context_handoff.md

If docs are outdated → DO NOT commit code.

---

### 14.4 Branching Strategy

Use structured branching:

* main → stable, working system
* dev → active development
* feature/<module-name> → specific work

Example:

```id="wt94ce"
feature/intent-resolution
feature/capability-graph
```

---

### 14.5 Pull Request Protocol

For every feature:

1. Create feature branch
2. Implement changes
3. Open PR into dev
4. Review (self-check via constitution)
5. Merge only if:

   * build passes
   * docs updated
   * architecture intact

---

### 14.6 Repository Must Always Be in Working State

At any point:

* Code must compile
* CLI must run
* Core flow must not break

Never push broken builds to main.

---

### 14.7 Context Continuity Enforcement

After each push:

* context_handoff.md MUST reflect:

  * latest changes
  * current system state
  * next steps
  * blockers

This ensures any new engineer can resume instantly.

---

### 14.8 Failure Conditions

The agent FAILS if:

* Code exists locally but not pushed
* Docs are not updated
* Commits are unclear or large and messy
* Repository cannot be built
* No clear development history

---

### FINAL RULE

> If it is not committed, documented, and pushed — it does not exist.

The repository is the **single source of truth**.
All work must be reflected there in real-time.

---

## FINAL RULE

If it improves structure → DO IT
If it introduces chaos → REJECT IT

---

End of Instructions
