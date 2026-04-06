#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

#include "Adapter.h"
#include "EventBus.h"
#include "ExecutionEngine.h"
#include "Intent.h"
#include "Telemetry.h"
#include "TestHelpers.h"

namespace {

class AlwaysFailAdapter final : public iee::Adapter {
public:
    std::string Name() const override {
        return "AlwaysFailAdapter";
    }

    std::vector<iee::Intent> GetCapabilities(const iee::ObserverSnapshot&, const iee::CapabilityGraph&) override {
        return {};
    }

    bool CanExecute(const iee::Intent& intent) const override {
        return intent.action == iee::IntentAction::Create;
    }

    iee::ExecutionResult Execute(const iee::Intent&) override {
        iee::ExecutionResult result;
        result.status = iee::ExecutionStatus::FAILED;
        result.verified = false;
        result.method = Name();
        result.message = "Forced failure";
        result.duration = std::chrono::milliseconds(5);
        return result;
    }

    iee::AdapterScore GetScore() const override {
        return iee::AdapterScore{0.95F, 5.0F, 0.95F};
    }
};

class DisappearingUiAdapter final : public iee::Adapter {
public:
    std::string Name() const override {
        return "DisappearingUiAdapter";
    }

    std::vector<iee::Intent> GetCapabilities(const iee::ObserverSnapshot&, const iee::CapabilityGraph&) override {
        return {};
    }

    bool CanExecute(const iee::Intent& intent) const override {
        return intent.action == iee::IntentAction::Activate;
    }

    iee::ExecutionResult Execute(const iee::Intent&) override {
        iee::ExecutionResult result;
        result.status = iee::ExecutionStatus::PARTIAL;
        result.verified = false;
        result.method = Name();
        result.message = "UI element disappeared during execution";
        result.duration = std::chrono::milliseconds(3);
        return result;
    }

    iee::AdapterScore GetScore() const override {
        return iee::AdapterScore{0.85F, 3.0F, 0.90F};
    }
};

class SlowAdapter final : public iee::Adapter {
public:
    std::string Name() const override {
        return "SlowAdapter";
    }

    std::vector<iee::Intent> GetCapabilities(const iee::ObserverSnapshot&, const iee::CapabilityGraph&) override {
        return {};
    }

    bool CanExecute(const iee::Intent& intent) const override {
        return intent.action == iee::IntentAction::Activate;
    }

    iee::ExecutionResult Execute(const iee::Intent&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        iee::ExecutionResult result;
        result.status = iee::ExecutionStatus::SUCCESS;
        result.verified = true;
        result.method = Name();
        result.message = "Slow success";
        result.duration = std::chrono::milliseconds(50);
        return result;
    }

    iee::AdapterScore GetScore() const override {
        return iee::AdapterScore{0.90F, 50.0F, 0.85F};
    }
};

iee::Intent BuildCreateIntent(const std::filesystem::path& path) {
    iee::Intent intent;
    intent.action = iee::IntentAction::Create;
    intent.name = "create";
    intent.target.type = iee::TargetType::FileSystemPath;
    intent.target.path = path.wstring();
    intent.target.label = path.wstring();
    intent.params.values["path"] = path.wstring();
    intent.source = "test";
    intent.confidence = 1.0F;
    intent.constraints.allowFallback = true;
    intent.constraints.maxRetries = 1;
    return intent;
}

iee::Intent BuildUiIntent() {
    iee::Intent intent;
    intent.action = iee::IntentAction::Activate;
    intent.name = "activate";
    intent.target.type = iee::TargetType::UiElement;
    intent.target.label = L"Play";
    intent.source = "test";
    intent.confidence = 1.0F;
    return intent;
}

}  // namespace

int main() {
    try {
        {
            iee::EventBus eventBus;
            iee::AdapterRegistry adapters;
            adapters.RegisterAdapter(std::make_shared<AlwaysFailAdapter>());
            adapters.Register(std::make_unique<iee::FileSystemAdapter>());

            iee::IntentValidator validator;
            iee::Telemetry telemetry;
            iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

            const std::filesystem::path path = "failure_injection_create.txt";
            const iee::ExecutionResult result = engine.Execute(BuildCreateIntent(path));
            AssertTrue(result.status == iee::ExecutionStatus::SUCCESS, "Fallback adapter should recover forced failure");
            AssertTrue(std::filesystem::exists(path), "Fallback create should materialize file");
            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }

        {
            iee::EventBus eventBus;
            iee::AdapterRegistry adapters;
            adapters.RegisterAdapter(std::make_shared<DisappearingUiAdapter>());

            iee::IntentValidator validator;
            iee::Telemetry telemetry;
            iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

            iee::Intent intent = BuildUiIntent();
            intent.constraints.requiresVerification = false;
            intent.constraints.maxRetries = 2;
            intent.constraints.allowFallback = false;

            const iee::ExecutionResult result = engine.Execute(intent);
            AssertTrue(result.status == iee::ExecutionStatus::PARTIAL, "Disappearing target should return PARTIAL without verification");
            AssertTrue(result.attempts == 1, "PARTIAL without verification should not retry");
        }

        {
            iee::EventBus eventBus;
            iee::AdapterRegistry adapters;
            adapters.RegisterAdapter(std::make_shared<SlowAdapter>());

            iee::IntentValidator validator;
            iee::Telemetry telemetry;
            iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

            iee::Intent intent = BuildUiIntent();
            intent.constraints.timeoutMs = 10;
            intent.constraints.maxRetries = 0;
            intent.constraints.allowFallback = false;

            const iee::ExecutionResult result = engine.Execute(intent);
            AssertTrue(result.status == iee::ExecutionStatus::FAILED, "Slow execution should fail timeout gate");
            AssertTrue(result.message.find("Timeout exceeded") != std::string::npos, "Timeout failure should be explicit");
        }

        std::cout << "integration_failure_injection: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "integration_failure_injection: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
