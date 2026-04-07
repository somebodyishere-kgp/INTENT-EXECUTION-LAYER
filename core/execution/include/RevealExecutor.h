#pragma once

#include <string>
#include <vector>

#include "ExecutionEngine.h"
#include "IntentRegistry.h"
#include "InteractionGraph.h"

namespace iee {

struct RevealExecutionResult {
    bool attempted{false};
    bool success{true};
    int attemptedSteps{0};
    int completedSteps{0};
    std::string message;
    std::vector<ExecutionResult> stepResults;
};

class RevealExecutor {
public:
    RevealExecutor(ExecutionEngine& executionEngine, IntentRegistry& registry);

    RevealExecutionResult Execute(const InteractionNode& node, int maxRetriesPerStep = 2);

private:
    std::optional<InteractionNode> LookupNode(const std::string& nodeId);
    static bool IsNodeRevealed(const InteractionNode& node);
    static Intent BuildRevealIntent(
        const InteractionNode& sourceNode,
        const InteractionNode& stepTarget,
        const PlanStep& step);

    ExecutionEngine& executionEngine_;
    IntentRegistry& registry_;
};

}  // namespace iee
