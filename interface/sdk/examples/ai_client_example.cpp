#include <iostream>

#include "IEEClient.h"

namespace iee::examples {

void RunPlanningAndExecutionDemo(IEEClient& client) {
    const std::string aiState = client.GetStateAiJson();
    std::cout << "AI state projection: " << aiState << "\n";

    ClientExecuteRequest request;
    request.action = IntentAction::Create;
    request.path = L"sdk_example_output.txt";
    request.requiresVerification = true;

    const ClientExecuteResponse response = client.Execute(request);
    std::cout << "Execution status: " << ToString(response.contract.execution.status) << "\n";
    std::cout << "Contract stage : " << response.contract.stage << "\n";
    std::cout << "Message        : " << response.contract.execution.message << "\n";
}

}  // namespace iee::examples
