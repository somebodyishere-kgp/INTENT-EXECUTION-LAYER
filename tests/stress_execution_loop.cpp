#include <filesystem>
#include <iostream>
#include <memory>
#include <algorithm>
#include <vector>

#include "Adapter.h"
#include "EventBus.h"
#include "ExecutionEngine.h"
#include "Intent.h"
#include "Telemetry.h"
#include "TestHelpers.h"

namespace {

iee::Intent BuildCreateIntent(const std::filesystem::path& path) {
    iee::Intent intent;
    intent.action = iee::IntentAction::Create;
    intent.name = "create";
    intent.target.type = iee::TargetType::FileSystemPath;
    intent.target.path = path.wstring();
    intent.target.label = path.wstring();
    intent.params.values["path"] = path.wstring();
    intent.source = "stress";
    intent.confidence = 1.0F;
    intent.constraints.maxRetries = 0;
    return intent;
}

iee::Intent BuildDeleteIntent(const std::filesystem::path& path) {
    iee::Intent intent;
    intent.action = iee::IntentAction::Delete;
    intent.name = "delete";
    intent.target.type = iee::TargetType::FileSystemPath;
    intent.target.path = path.wstring();
    intent.target.label = path.wstring();
    intent.params.values["path"] = path.wstring();
    intent.source = "stress";
    intent.confidence = 1.0F;
    intent.constraints.maxRetries = 0;
    return intent;
}

}  // namespace

int main() {
    try {
        iee::EventBus eventBus;
        iee::AdapterRegistry adapters;
        adapters.Register(std::make_unique<iee::FileSystemAdapter>());

        iee::Telemetry telemetry;
        iee::IntentValidator validator;
        iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

        constexpr int kIterations = 1000;
        std::vector<std::int64_t> latencySamples;
        latencySamples.reserve(static_cast<std::size_t>(kIterations) * 2U);

        for (int i = 0; i < kIterations; ++i) {
            const std::filesystem::path path = "stress_file_" + std::to_string(i) + ".txt";

            const iee::ExecutionResult createResult = engine.Execute(BuildCreateIntent(path));
            AssertTrue(createResult.status == iee::ExecutionStatus::SUCCESS, "Create should succeed during stress loop");
            latencySamples.push_back(createResult.duration.count());

            const iee::ExecutionResult deleteResult = engine.Execute(BuildDeleteIntent(path));
            AssertTrue(deleteResult.status == iee::ExecutionStatus::SUCCESS, "Delete should succeed during stress loop");
            latencySamples.push_back(deleteResult.duration.count());

            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }

        std::sort(latencySamples.begin(), latencySamples.end());
        const std::size_t p50Index = (latencySamples.size() - 1U) * 50U / 100U;
        const std::size_t p95Index = (latencySamples.size() - 1U) * 95U / 100U;
        const std::size_t p99Index = (latencySamples.size() - 1U) * 99U / 100U;

        const std::int64_t p50 = latencySamples[p50Index];
        const std::int64_t p95 = latencySamples[p95Index];
        const std::int64_t p99 = latencySamples[p99Index];

        const iee::TelemetrySnapshot snapshot = telemetry.Snapshot();
        AssertTrue(snapshot.totalExecutions >= static_cast<std::uint64_t>(kIterations * 2), "Stress loop should emit telemetry samples");
        AssertTrue(p50 <= 16, "Expected p50 latency <= 16ms");
        AssertTrue(p95 <= 32, "Expected p95 latency <= 32ms");
        AssertTrue(p99 <= 64, "Expected p99 latency <= 64ms");

        std::cout << "latency p50=" << p50 << "ms p95=" << p95 << "ms p99=" << p99 << "ms\n";

        std::cout << "stress_execution_loop: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "stress_execution_loop: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
