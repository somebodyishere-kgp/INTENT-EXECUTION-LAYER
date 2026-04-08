# Issues and Errors Log (v2.0 Upgrade)

## Date
2026-04-08

## Resolved During v2.0 Migration

### 1. Self-healing node intent mapping referenced non-existent field
- Symptom: Release build failed in `ActionInterface.cpp` with missing `automationId` member on `InteractionNode`.
- Root cause: recovery intent reconstruction attempted to access node fields outside interaction-node contract.
- Fix: removed invalid assignment and kept recovery intent reconstruction on stable node/id/label fields.
- Result: self-healing path compiles and runs under current node schema.

### 2. Perception extension used undefined UTF helper
- Symptom: Release build failed in `EnvironmentAdapter.cpp` (`Narrow` not found).
- Root cause: new lightweight text heuristics required UTF-16 to UTF-8 conversion helper but helper was not declared in that translation unit.
- Fix: added local deterministic `Narrow(const std::wstring&)` helper.
- Result: perception extension compiles and emits expected text/grouping fields.

### 3. CMake Tools extension build/test integration unavailable in current VS Code session
- Symptom: `Build_CMakeTools` and `RunCtest_CMakeTools` returned "Unable to configure the project".
- Root cause: local CMake Tools extension state mismatch (outside codebase logic).
- Fix: validated with direct command-line build/test commands.
- Result: Release build and full CTest suite validated successfully.

### 4. v2 routes initially lacked dedicated regression assertions
- Symptom: new v2 endpoints existed but were not explicitly asserted in API hardening suite.
- Root cause: route implementation landed before parity test expansion.
- Fix: extended `integration_api_hardening` for policy, sequence/workflow/semantic, UCP, adapter metadata, execution memory, percentiles, and frame consistency routes.
- Result: v2 route contracts are now regression guarded.

## Current Known Risks (Non-Blocking)

### 1. Execution memory is process-local
- Current behavior: `ExecutionMemoryStore` resets on restart.
- Risk: long-run adaptation does not persist across process boundaries.

### 2. Semantic bridge is deterministic heuristic mode
- Current behavior: semantic decomposition uses bounded parser/rule logic.
- Risk: very abstract goals can under-specify desired decomposition without explicit lexical cues.

### 3. Policy parser is intentionally narrow
- Current behavior: API parser expects flat JSON object with string values.
- Risk: clients sending nested/typed payloads need a compatibility shim.

### 4. UCP surface is envelope-level (v1)
- Current behavior: UCP routes return stable v1 envelopes.
- Risk: protocol negotiation/versioning beyond `ucp_version:1.0` is future work.
