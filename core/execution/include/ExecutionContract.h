#pragma once

#include <string>

#include "ExecutionEngine.h"
#include "RevealExecutor.h"

namespace iee {

struct ExecutionContractResult {
    ExecutionResult execution;
    RevealExecutionResult reveal;
    bool revealRequired{false};
    bool verificationPassed{false};
    bool contractSatisfied{false};
    std::string stage;
    std::string message;
};

class ExecutionContract {
public:
    ExecutionContract(ExecutionEngine& executionEngine, IntentRegistry& registry);

    ExecutionContractResult Execute(const Intent& intent, const std::string& nodeId = "");

private:
    std::optional<InteractionNode> ResolveNode(const std::string& nodeId);
    static bool VerifyOutcome(const Intent& intent, const ExecutionResult& result);

    ExecutionEngine& executionEngine_;
    IntentRegistry& registry_;
    RevealExecutor revealExecutor_;
};

}  // namespace iee
