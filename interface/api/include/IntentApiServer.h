#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "ExecutionEngine.h"
#include "IntentRegistry.h"
#include "Telemetry.h"

namespace iee {

class IntentApiServer {
public:
    IntentApiServer(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry);

    int Run(std::uint16_t port, bool singleRequest = false, std::size_t maxRequests = 0);
    std::string HandleRequestForTesting(const std::string& request);

private:
    std::string HandleRequest(const std::string& request);

    IntentRegistry& registry_;
    ExecutionEngine& executionEngine_;
    Telemetry& telemetry_;
    std::chrono::steady_clock::time_point startedAt_;
};

}  // namespace iee
