#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "EnvironmentAdapter.h"
#include "ExecutionEngine.h"
#include "Intent.h"

namespace iee {

struct ActionStep {
    Intent intent;
    std::string label;
    int delayAfterMs{0};
    bool required{true};
    std::size_t repeatCount{1};
    std::wstring ifTargetVisible;
    std::optional<Intent> elseIntent;
};

struct ActionSequence {
    std::string id;
    std::vector<ActionStep> steps;
    bool stopOnFailure{true};
};

struct ActionSequenceResult {
    bool success{false};
    std::string sequenceId;
    std::size_t completedSteps{0};
    std::size_t attemptedSteps{0};
    std::chrono::milliseconds totalDuration{0};
    std::vector<ExecutionResult> stepResults;
    std::string message;
};

ActionSequence BuildRepeatedSequence(
    const Intent& templateIntent,
    std::size_t repeat,
    const std::string& sequenceId = "");

bool ParseActionSequenceDsl(
    const std::string& sequenceDsl,
    const Intent& templateIntent,
    ActionSequence* sequence,
    std::string* error = nullptr);

class MacroExecutor {
public:
    explicit MacroExecutor(ExecutionEngine& executionEngine);

    ActionSequenceResult Execute(const ActionSequence& sequence, const EnvironmentState* synchronizedState = nullptr);

private:
    ExecutionEngine& executionEngine_;
};

}  // namespace iee
