#include "IEEClient.h"

namespace iee {

IEEClient::IEEClient(IntentRegistry& registry, ExecutionEngine& executionEngine)
    : registry_(registry),
      environmentAdapter_(registry_),
      executionContract_(executionEngine, registry_) {}

EnvironmentState IEEClient::GetState() {
    EnvironmentState state;
    std::string error;
    if (!environmentAdapter_.CaptureState(&state, &error)) {
        EnvironmentState failed;
        failed.valid = false;
        return failed;
    }

    return state;
}

std::string IEEClient::GetStateAiJson() {
    const EnvironmentState state = GetState();
    const AIStateView view = stateProjector_.Build(state, false);
    return AIStateViewProjector::SerializeJson(view);
}

ClientExecuteResponse IEEClient::Execute(const ClientExecuteRequest& request) {
    ClientExecuteResponse response;
    response.intent = BuildIntent(request);
    response.contract = executionContract_.Execute(response.intent, request.nodeId);
    return response;
}

Intent IEEClient::BuildIntent(const ClientExecuteRequest& request) {
    Intent intent;
    intent.action = request.action;
    intent.name = ToString(request.action);
    intent.source = "sdk";
    intent.confidence = 1.0F;
    intent.constraints.timeoutMs = request.timeoutMs;
    intent.constraints.maxRetries = request.maxRetries;
    intent.constraints.requiresVerification = request.requiresVerification;

    if (request.action == IntentAction::Activate || request.action == IntentAction::SetValue ||
        request.action == IntentAction::Select) {
        intent.target.type = TargetType::UiElement;
        intent.target.label = request.target;
    } else {
        intent.target.type = TargetType::FileSystemPath;
        intent.target.path = request.path;
        intent.target.label = request.path;
        intent.params.values["path"] = request.path;
    }

    intent.target.nodeId = request.nodeId;

    if (request.action == IntentAction::SetValue) {
        intent.params.values["value"] = request.value;
    }

    if (request.action == IntentAction::Move) {
        intent.params.values["destination"] = request.destination;
    }

    return intent;
}

}  // namespace iee
