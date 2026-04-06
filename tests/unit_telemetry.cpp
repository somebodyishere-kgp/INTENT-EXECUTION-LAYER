#include <chrono>
#include <iostream>

#include "Telemetry.h"
#include "TestHelpers.h"

int main() {
    try {
        iee::Telemetry telemetry;

        const std::string traceId = telemetry.NewTraceId();
        telemetry.LogAdapterDecision(
            traceId,
            iee::IntentAction::Activate,
            L"Save",
            "UIAAdapter",
            {{"UIAAdapter", 0.91F}, {"FileSystemAdapter", 0.10F}},
            false);

        iee::ExecutionTrace trace;
        trace.traceId = traceId;
        trace.intent = "activate";
        trace.target = L"Save";
        trace.adapter = "UIAAdapter";
        trace.durationMs = 42;
        trace.status = "SUCCESS";
        trace.message = "ok";
        trace.verified = true;
        trace.attempts = 1;
        trace.timestamp = std::chrono::system_clock::now();
        telemetry.LogExecution(trace);

        telemetry.LogResolutionTiming(std::chrono::milliseconds(3));

        const iee::TelemetrySnapshot snapshot = telemetry.Snapshot();
        AssertTrue(snapshot.totalExecutions == 1, "Expected one execution sample");
        AssertTrue(snapshot.successCount == 1, "Expected one success sample");
        AssertTrue(snapshot.failureCount == 0, "Expected zero failures");
        AssertTrue(snapshot.averageLatencyMs >= 42.0, "Expected latency aggregation");
        AssertTrue(snapshot.averageResolutionMs >= 3.0, "Expected resolution aggregation");

        const auto found = telemetry.FindTrace(traceId);
        AssertTrue(found.has_value(), "Expected trace lookup to succeed");
        AssertTrue(found->adapter == "UIAAdapter", "Expected adapter in trace");

        const std::string snapshotJson = telemetry.SerializeSnapshotJson();
        AssertTrue(snapshotJson.find("totalExecutions") != std::string::npos, "Snapshot JSON should include totals");

        const std::string traceJson = telemetry.SerializeTraceJson(traceId);
        AssertTrue(traceJson.find(traceId) != std::string::npos, "Trace JSON should include trace id");

        std::cout << "unit_telemetry: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "unit_telemetry: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
