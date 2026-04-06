#pragma once

#include "CliParser.h"
#include "ExecutionEngine.h"
#include "IntentRegistry.h"
#include "Telemetry.h"

namespace iee {

class CliApp {
public:
    CliApp(IntentRegistry& intentRegistry, ExecutionEngine& executionEngine, Telemetry& telemetry);

    int Run(int argc, char* argv[]);

private:
    int HandleListIntents();
    int HandleExecute(const ParsedCommand& command);
    int HandleInspect();
    int HandleExplain(const ParsedCommand& command);
    int HandleDebugIntents(const ParsedCommand& command);
    int HandleApi(const ParsedCommand& command);
    int HandleTelemetry(const ParsedCommand& command);
    int HandleTrace(const ParsedCommand& command);
    int HandleLatency(const ParsedCommand& command);
    int HandlePerf(const ParsedCommand& command);

    IntentRegistry& intentRegistry_;
    ExecutionEngine& executionEngine_;
    Telemetry& telemetry_;
};

}  // namespace iee
