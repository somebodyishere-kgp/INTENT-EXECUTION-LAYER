#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "Adapter.h"
#include "CapabilityGraph.h"
#include "ControlRuntime.h"
#include "EventBus.h"
#include "ExecutionEngine.h"
#include "Intent.h"
#include "IntentRegistry.h"
#include "IntentResolver.h"
#include "ObserverEngine.h"
#include "Telemetry.h"
#include "TestHelpers.h"

namespace {

class FakeObserver final : public iee::IObserverEngine {
public:
    iee::ObserverSnapshot Capture() override {
        iee::ObserverSnapshot snapshot;
        snapshot.valid = true;
        snapshot.sequence = ++sequence_;
        snapshot.activeWindowTitle = L"ControlRuntimeTest";
        snapshot.activeProcessPath = L"control_runtime_test.exe";
        snapshot.cursorPosition = {120, 80};
        return snapshot;
    }

private:
    std::uint64_t sequence_{0};
};

class FastUiAdapter final : public iee::Adapter {
public:
    std::string Name() const override {
        return "UIAAdapter";
    }

    std::vector<iee::Intent> GetCapabilities(const iee::ObserverSnapshot& snapshot, const iee::CapabilityGraph&) override {
        iee::Intent intent;
        intent.id = "uia-control-save";
        intent.name = "activate";
        intent.action = iee::IntentAction::Activate;
        intent.source = "uia";
        intent.confidence = 1.0F;
        intent.target.type = iee::TargetType::UiElement;
        intent.target.label = L"Save";
        intent.target.focused = true;
        intent.target.hierarchyDepth = 1;
        intent.target.screenCenter = snapshot.cursorPosition;
        intent.context.cursor = snapshot.cursorPosition;
        intent.context.snapshotVersion = snapshot.sequence;
        intent.context.snapshotTicks = snapshot.sequence;
        intent.constraints.maxRetries = 0;
        intent.constraints.timeoutMs = 8;
        return {intent};
    }

    bool CanExecute(const iee::Intent& intent) const override {
        return intent.action == iee::IntentAction::Activate && intent.target.type == iee::TargetType::UiElement;
    }

    iee::ExecutionResult Execute(const iee::Intent&) override {
        iee::ExecutionResult result;
        result.status = iee::ExecutionStatus::SUCCESS;
        result.verified = true;
        result.method = Name();
        result.message = "ok";
        result.duration = std::chrono::milliseconds(1);
        return result;
    }

    iee::AdapterScore GetScore() const override {
        return iee::AdapterScore{0.95F, 1.0F, 0.95F};
    }
};

iee::Intent BuildRuntimeIntent() {
    iee::Intent intent;
    intent.id = "runtime-activate";
    intent.name = "activate";
    intent.action = iee::IntentAction::Activate;
    intent.source = "integration-test";
    intent.confidence = 1.0F;
    intent.target.type = iee::TargetType::UiElement;
    intent.target.label = L"Save";
    intent.target.screenCenter = {120, 80};
    intent.constraints.timeoutMs = 8;
    intent.constraints.maxRetries = 0;
    intent.constraints.allowFallback = false;
    return intent;
}

}  // namespace

int main() {
    try {
        iee::EventBus eventBus;
        iee::AdapterRegistry adapters;
        adapters.RegisterAdapter(std::make_shared<FastUiAdapter>());

        FakeObserver observer;
        iee::CapabilityGraphBuilder graphBuilder;
        iee::IntentResolver resolver;
        iee::Telemetry telemetry;
        iee::IntentRegistry registry(observer, adapters, graphBuilder, resolver, eventBus, telemetry);
        iee::IntentValidator validator;
        iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

        iee::ControlRuntime runtime(registry, engine, eventBus, telemetry);

        iee::ControlRuntimeConfig config;
        config.targetFrameMs = 1;
        config.maxFrames = 1100;

        std::string message;
        const bool started = runtime.Start(config, &message);
        AssertTrue(started, "Expected control runtime to start");

        for (int i = 0; i < 1000; ++i) {
            const iee::ControlPriority priority = (i % 10 == 0) ? iee::ControlPriority::High : iee::ControlPriority::Medium;
            AssertTrue(runtime.EnqueueIntent(BuildRuntimeIntent(), priority), "Expected enqueue to succeed");
        }

        for (int i = 0; i < 20; ++i) {
            eventBus.Publish(iee::Event{
                iee::EventType::UiChanged,
                "integration_test",
                "priority event",
                std::chrono::system_clock::now(),
                iee::EventPriority::HIGH});
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        while (std::chrono::steady_clock::now() < deadline) {
            const iee::ControlRuntimeSnapshot status = runtime.Status();
            if (!status.active || status.intentsExecuted >= 1000) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        const iee::ControlRuntimeSummary summary = runtime.Stop();
        const iee::ControlRuntimeSnapshot finalStatus = runtime.Status();

        AssertTrue(summary.framesExecuted >= 300, "Expected sustained control loop execution");
        AssertTrue(summary.intentsExecuted >= 600, "Expected queued intents to be executed");
        AssertTrue(summary.p50LatencyMs <= 8.0, "Expected p50 cycle latency <= 8ms");
        AssertTrue(summary.p95LatencyMs <= 20.0, "Expected p95 cycle latency <= 20ms");
        AssertTrue(summary.p99LatencyMs <= 35.0, "Expected p99 cycle latency <= 35ms");
        AssertTrue(finalStatus.highPriorityEvents > 0, "Expected high-priority events to be processed");

        std::cout << "integration_control_runtime: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "integration_control_runtime: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
