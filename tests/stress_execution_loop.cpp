#include <filesystem>
#include <iostream>
#include <memory>

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

        constexpr int kIterations = 40;
        for (int i = 0; i < kIterations; ++i) {
            const std::filesystem::path path = "stress_file_" + std::to_string(i) + ".txt";

            const iee::ExecutionResult createResult = engine.Execute(BuildCreateIntent(path));
            AssertTrue(createResult.status == iee::ExecutionStatus::SUCCESS, "Create should succeed during stress loop");

            const iee::ExecutionResult deleteResult = engine.Execute(BuildDeleteIntent(path));
            AssertTrue(deleteResult.status == iee::ExecutionStatus::SUCCESS, "Delete should succeed during stress loop");

            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }

        const iee::TelemetrySnapshot snapshot = telemetry.Snapshot();
        AssertTrue(snapshot.totalExecutions >= static_cast<std::uint64_t>(kIterations * 2), "Stress loop should emit telemetry samples");

        std::cout << "stress_execution_loop: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "stress_execution_loop: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
