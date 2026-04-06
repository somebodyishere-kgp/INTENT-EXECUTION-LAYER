#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "EnvironmentAdapter.h"
#include "ControlRuntime.h"
#include "DecisionInterfaces.h"
#include "ExecutionEngine.h"
#include "IntentRegistry.h"
#include "Telemetry.h"

namespace iee {

class IntentApiServer {
public:
    IntentApiServer(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry);
    void SetPredictor(std::shared_ptr<Predictor> predictor);

    int Run(std::uint16_t port, bool singleRequest = false, std::size_t maxRequests = 0);
    std::string HandleRequestForTesting(const std::string& request);

private:
    std::string HandleRequest(const std::string& request);
    ControlRuntime& EnsureControlRuntime();

    IntentRegistry& registry_;
    ExecutionEngine& executionEngine_;
    Telemetry& telemetry_;
    std::unique_ptr<ControlRuntime> controlRuntime_;
    std::shared_ptr<EnvironmentAdapter> streamEnvironmentAdapter_;
    mutable std::mutex predictorMutex_;
    std::shared_ptr<Predictor> predictor_;
    std::chrono::steady_clock::time_point startedAt_;
};

}  // namespace iee
