#include "RevealExecutor.h"

#include <Windows.h>

#include <algorithm>

namespace iee {
namespace {

std::wstring Wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int requiredChars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (requiredChars <= 1) {
        return L"";
    }

    std::wstring result(static_cast<std::size_t>(requiredChars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), requiredChars);
    result.pop_back();
    return result;
}

}  // namespace

RevealExecutor::RevealExecutor(ExecutionEngine& executionEngine, IntentRegistry& registry)
    : executionEngine_(executionEngine), registry_(registry) {}

RevealExecutionResult RevealExecutor::Execute(const InteractionNode& node, int maxRetriesPerStep) {
    RevealExecutionResult result;

    if (!node.revealStrategy.required) {
        result.message = "reveal_not_required";
        return result;
    }

    result.attempted = true;
    if (IsNodeRevealed(node)) {
        result.message = "already_revealed";
        return result;
    }

    const int boundedRetries = std::clamp(maxRetriesPerStep, 0, 5);

    for (const PlanStep& step : node.revealStrategy.steps) {
        ++result.attemptedSteps;
        bool stepCompleted = false;

        if (step.action == "probe_visibility") {
            ++result.totalStepAttempts;
            const auto probeNode = LookupNode(node.id);
            stepCompleted = probeNode.has_value() && IsNodeRevealed(*probeNode);
        } else {
            for (int attempt = 0; attempt <= boundedRetries; ++attempt) {
                const auto targetNode = LookupNode(step.targetId.empty() ? node.id : step.targetId);
                const InteractionNode stepTarget = targetNode.value_or(node);

                ExecutionResult revealStep = executionEngine_.Execute(BuildRevealIntent(node, stepTarget, step));
                ++result.totalStepAttempts;
                if (revealStep.usedFallback) {
                    result.fallbackUsed = true;
                    ++result.fallbackStepCount;
                }
                result.stepResults.push_back(revealStep);

                if (revealStep.IsSuccess()) {
                    stepCompleted = true;
                    break;
                }
            }
        }

        if (!stepCompleted) {
            result.success = false;
            result.message = "reveal_step_failed:" + step.id;
            return result;
        }

        ++result.completedSteps;
    }

    const auto refreshedNode = LookupNode(node.id);
    if (!refreshedNode.has_value() || !IsNodeRevealed(*refreshedNode)) {
        result.success = false;
        result.message = "reveal_verification_failed";
        return result;
    }

    result.success = true;
    result.message = "reveal_completed";
    return result;
}

std::optional<InteractionNode> RevealExecutor::LookupNode(const std::string& nodeId) {
    if (nodeId.empty()) {
        return std::nullopt;
    }

    registry_.Refresh();
    const ObserverSnapshot snapshot = registry_.LastSnapshot();
    if (!snapshot.valid) {
        return std::nullopt;
    }

    const InteractionGraph graph = InteractionGraphBuilder::Build(snapshot.uiElements, snapshot.sequence);
    return InteractionGraphBuilder::FindNode(graph, nodeId);
}

bool RevealExecutor::IsNodeRevealed(const InteractionNode& node) {
    return !node.hidden && !node.offscreen && !node.collapsed && node.visible;
}

Intent RevealExecutor::BuildRevealIntent(
    const InteractionNode& sourceNode,
    const InteractionNode& stepTarget,
    const PlanStep& step) {
    Intent intent;
    intent.action = IntentAction::Activate;
    intent.name = ToString(IntentAction::Activate);
    intent.source = "reveal_executor";
    intent.confidence = 1.0F;
    intent.target.type = TargetType::UiElement;
    intent.target.label = Wide(stepTarget.label.empty() ? sourceNode.label : stepTarget.label);
    intent.target.nodeId = step.targetId.empty() ? sourceNode.id : step.targetId;
    intent.constraints.timeoutMs = 1200;
    intent.constraints.maxRetries = 0;
    intent.constraints.allowFallback = true;
    intent.constraints.requiresVerification = false;

    intent.params.values["reveal_action"] = Wide(step.action);
    if (!step.argument.empty()) {
        intent.params.values["argument"] = Wide(step.argument);
    }

    return intent;
}

}  // namespace iee
