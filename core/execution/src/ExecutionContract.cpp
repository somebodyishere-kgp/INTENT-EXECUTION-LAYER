#include "ExecutionContract.h"

#include <filesystem>

namespace iee {

ExecutionContract::ExecutionContract(ExecutionEngine& executionEngine, IntentRegistry& registry)
    : executionEngine_(executionEngine), registry_(registry), revealExecutor_(executionEngine, registry) {}

ExecutionContractResult ExecutionContract::Execute(const Intent& intent, const std::string& nodeId) {
    ExecutionContractResult contractResult;

    const std::string resolvedNodeId = nodeId.empty() ? intent.target.nodeId : nodeId;
    if (!resolvedNodeId.empty()) {
        const auto node = ResolveNode(resolvedNodeId);
        if (!node.has_value()) {
            contractResult.execution.status = ExecutionStatus::FAILED;
            contractResult.execution.verified = false;
            contractResult.execution.method = "execution_contract";
            contractResult.execution.message = "Execution contract node was not found";
            contractResult.stage = "resolve";
            contractResult.message = "node_not_found";
            return contractResult;
        }

        contractResult.revealRequired = node->revealStrategy.required;
        if (contractResult.revealRequired) {
            contractResult.reveal = revealExecutor_.Execute(*node);
            if (!contractResult.reveal.success) {
                if (contractResult.reveal.message == "reveal_verification_failed" &&
                    contractResult.reveal.completedSteps == contractResult.reveal.attemptedSteps) {
                    contractResult.reveal.success = true;
                    contractResult.reveal.message = "reveal_verification_deferred";
                } else {
                contractResult.execution.status = ExecutionStatus::FAILED;
                contractResult.execution.verified = false;
                contractResult.execution.method = "execution_contract";
                contractResult.execution.message = "Reveal stage failed: " + contractResult.reveal.message;
                contractResult.stage = "reveal";
                contractResult.message = contractResult.reveal.message;
                return contractResult;
                }
            }
        }
    }

    contractResult.execution = executionEngine_.Execute(intent);
    contractResult.verificationPassed = VerifyOutcome(intent, contractResult.execution);

    if (!contractResult.verificationPassed) {
        if (contractResult.execution.status != ExecutionStatus::FAILED) {
            contractResult.execution.status = ExecutionStatus::FAILED;
            contractResult.execution.verified = false;
            if (!contractResult.execution.message.empty()) {
                contractResult.execution.message += "; ";
            }
            contractResult.execution.message += "Execution verification failed under contract";
        }

        contractResult.stage = "verify";
        contractResult.message = "verification_failed";
        return contractResult;
    }

    contractResult.stage = "verified";
    contractResult.message = "contract_satisfied";
    contractResult.contractSatisfied = contractResult.execution.IsSuccess() && contractResult.verificationPassed;
    return contractResult;
}

std::optional<InteractionNode> ExecutionContract::ResolveNode(const std::string& nodeId) {
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

bool ExecutionContract::VerifyOutcome(const Intent& intent, const ExecutionResult& result) {
    if (result.status == ExecutionStatus::FAILED) {
        return false;
    }

    if (!intent.constraints.requiresVerification) {
        return true;
    }

    if (result.verified) {
        return true;
    }

    std::error_code ec;
    if (intent.action == IntentAction::Create) {
        const std::filesystem::path path(intent.target.path);
        return std::filesystem::exists(path, ec);
    }

    if (intent.action == IntentAction::Delete) {
        const std::filesystem::path path(intent.target.path);
        return !std::filesystem::exists(path, ec);
    }

    if (intent.action == IntentAction::Move) {
        const auto destinationIt = intent.params.values.find("destination");
        if (destinationIt == intent.params.values.end()) {
            return false;
        }

        const std::filesystem::path destination(destinationIt->second);
        return std::filesystem::exists(destination, ec);
    }

    return false;
}

}  // namespace iee
