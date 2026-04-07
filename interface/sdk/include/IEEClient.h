#pragma once

#include <string>

#include "AIStateView.h"
#include "EnvironmentAdapter.h"
#include "ExecutionContract.h"
#include "IntentRegistry.h"

namespace iee {

struct ClientExecuteRequest {
    IntentAction action{IntentAction::Unknown};
    std::wstring target;
    std::wstring value;
    std::wstring path;
    std::wstring destination;
    std::string nodeId;
    int timeoutMs{2500};
    int maxRetries{2};
    bool requiresVerification{true};
};

struct ClientExecuteResponse {
    Intent intent;
    ExecutionContractResult contract;
};

class IEEClient {
public:
    IEEClient(IntentRegistry& registry, ExecutionEngine& executionEngine);

    EnvironmentState GetState();
    std::string GetStateAiJson();
    ClientExecuteResponse Execute(const ClientExecuteRequest& request);

private:
    static Intent BuildIntent(const ClientExecuteRequest& request);

    IntentRegistry& registry_;
    RegistryEnvironmentAdapter environmentAdapter_;
    ExecutionContract executionContract_;
    AIStateViewProjector stateProjector_;
};

}  // namespace iee
