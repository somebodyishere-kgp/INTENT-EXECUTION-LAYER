#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ExecutionContract.h"
#include "IntentRegistry.h"
#include "TaskInterface.h"
#include "Telemetry.h"

namespace iee {

struct ActionContextHints {
    std::string app;
    std::string domain;
};

struct ActionRequest {
    std::string action;
    std::string target;
    std::string value;
    ActionContextHints context;
};

struct ActionResolutionCandidate {
    std::string nodeId;
    std::string label;
    double confidence{0.0};
};

struct TargetResolution {
    bool matched{false};
    bool ambiguous{false};
    std::string nodeId;
    double confidence{0.0};
    std::vector<ActionResolutionCandidate> alternatives;
};

class TargetResolver {
public:
    explicit TargetResolver(IntentRegistry& registry);

    TargetResolution Resolve(
        const std::string& query,
        const ActionContextHints& context,
        const std::string& action,
        std::size_t maxCandidates = 8U) const;

    static void RecordSuccessfulResolution(
        const std::string& query,
        const ActionContextHints& context,
        const std::string& nodeId);

private:
    static std::string BuildMemoryKey(const std::string& query, const ActionContextHints& context);

    IntentRegistry& registry_;
};

struct ActionExecutionResult {
    std::string status{"failure"};
    std::string traceId;
    std::string reason;
    std::string resolvedNodeId;

    bool hasPlan{false};
    ExecutionPlan planUsed;

    bool revealUsed{false};
    bool verified{false};
    bool contractSatisfied{false};

    std::string executionStatus;
    std::string executionMethod;
    std::string executionMessage;

    bool usedFallback{false};
    std::vector<ActionResolutionCandidate> candidates;
};

class ActionExecutor {
public:
    ActionExecutor(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry);

    ActionExecutionResult Act(const ActionRequest& request);

private:
    static bool ParseActionName(
        const std::string& action,
        IntentAction* mappedAction,
        bool* navigateAction,
        std::string* normalizedAction);

    static std::string NormalizeAction(const std::string& action);

    Intent BuildIntentForAction(
        const ActionRequest& request,
        const InteractionNode& resolvedNode,
        const ObserverSnapshot& snapshot,
        bool navigateAction,
        const std::string& navigationValue,
        IntentAction mappedAction) const;

    IntentRegistry& registry_;
    ExecutionEngine& executionEngine_;
    Telemetry& telemetry_;
    TargetResolver resolver_;
};

bool ParseActionRequestJson(std::string_view payload, ActionRequest* request, std::string* error);
std::string SerializeActionExecutionResultJson(const ActionExecutionResult& result);

}  // namespace iee
