#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

#include "Adapter.h"
#include "CapabilityGraph.h"
#include "ControlRuntime.h"
#include "DecisionInterfaces.h"
#include "EventBus.h"
#include "ExecutionEngine.h"
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
        snapshot.activeWindowTitle = L"ClosedLoopFeedback";
        snapshot.activeProcessPath = L"closed_loop_feedback.exe";
        snapshot.cursorPosition = {64, 64};
        return snapshot;
    }

private:
    std::uint64_t sequence_{0};
};

iee::EnvironmentState BuildStableState() {
    iee::EnvironmentState state;
    state.sequence = 1;
    state.sourceSnapshotVersion = 1;
    state.activeWindowTitle = L"ClosedLoopFeedback";
    state.activeProcessPath = L"closed_loop_feedback.exe";
    state.cursorPosition = {64, 64};
    state.simulated = true;
    state.valid = true;

    iee::UiElement saveButton;
    saveButton.id = "btn-save";
    saveButton.name = L"Save";
    saveButton.controlType = iee::UiControlType::Button;
    saveButton.bounds = RECT{10, 10, 150, 45};
    saveButton.isFocused = true;
    saveButton.isEnabled = true;
    state.uiElements.push_back(saveButton);

    state.perception = iee::LightweightPerception::Analyze(state);
    return state;
}

class SingleShotCreateDecisionProvider final : public iee::DecisionProvider {
public:
    explicit SingleShotCreateDecisionProvider(std::wstring outputPath)
        : outputPath_(std::move(outputPath)) {}

    std::string Name() const override {
        return "single_shot_create";
    }

    std::vector<iee::Intent> Decide(
        const iee::StateSnapshot&,
        std::chrono::milliseconds budget,
        std::string* diagnostics) override {
        ++calls_;

        if (emitted_.exchange(true)) {
            if (diagnostics != nullptr) {
                *diagnostics = "single-shot intent already emitted";
            }
            return {};
        }

        iee::Intent intent;
        intent.action = iee::IntentAction::Create;
        intent.name = "create";
        intent.source = "decision-provider";
        intent.confidence = 1.0F;
        intent.target.type = iee::TargetType::FileSystemPath;
        intent.target.path = outputPath_;
        intent.target.label = outputPath_;
        intent.params.values["path"] = outputPath_;
        intent.constraints.maxRetries = 0;
        intent.constraints.timeoutMs = static_cast<int>(std::max<long long>(4, budget.count() * 4));

        if (diagnostics != nullptr) {
            *diagnostics = "single-shot create intent emitted";
        }

        return {intent};
    }

    int CallCount() const {
        return calls_.load();
    }

private:
    std::wstring outputPath_;
    std::atomic<bool> emitted_{false};
    std::atomic<int> calls_{0};
};

}  // namespace

int main() {
    try {
        const std::filesystem::path outputFile = "closed_loop_feedback_output.txt";
        if (std::filesystem::exists(outputFile)) {
            std::filesystem::remove(outputFile);
        }

        iee::EventBus eventBus;
        iee::AdapterRegistry adapters;
        adapters.Register(std::make_unique<iee::FileSystemAdapter>());

        FakeObserver observer;
        iee::CapabilityGraphBuilder graphBuilder;
        iee::IntentResolver resolver;
        iee::Telemetry telemetry;

        iee::IntentRegistry registry(observer, adapters, graphBuilder, resolver, eventBus, telemetry);
        iee::IntentValidator validator;
        iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

        auto environmentAdapter = std::make_shared<iee::MockEnvironmentAdapter>(
            std::vector<iee::EnvironmentState>{BuildStableState()});

        iee::ControlRuntime runtime(registry, engine, eventBus, telemetry, environmentAdapter);
        auto decisionProvider = std::make_shared<SingleShotCreateDecisionProvider>(outputFile.wstring());
        runtime.SetDecisionProvider(decisionProvider, 2);

        iee::ControlRuntimeConfig config;
        config.targetFrameMs = 2;
        config.maxFrames = 1000;
        config.observationIntervalMs = 1;
        config.decisionBudgetMs = 2;

        std::string startMessage;
        const bool started = runtime.Start(config, &startMessage);
        AssertTrue(started, "Expected control runtime to start for closed-loop feedback test");

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (std::chrono::steady_clock::now() < deadline) {
            const iee::ControlRuntimeSnapshot status = runtime.Status();
            if (status.feedbackCorrections > 0 || status.intentsExecuted >= 2) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        const iee::ControlRuntimeSummary summary = runtime.Stop();
        const iee::ControlRuntimeSnapshot finalStatus = runtime.Status();

        AssertTrue(finalStatus.decisionIntentsProduced >= 1, "Expected decision provider to enqueue at least one intent");
        AssertTrue(finalStatus.feedbackSamples >= 1, "Expected at least one feedback sample");
        AssertTrue(finalStatus.feedbackMismatches >= 1, "Expected feedback mismatch detection");
        AssertTrue(finalStatus.feedbackCorrections >= 1, "Expected at least one scheduled correction");
        AssertTrue(summary.p95LatencyMs <= 30.0, "Expected closed-loop runtime to remain within latency bounds");
        AssertTrue(std::filesystem::exists(outputFile), "Decision-driven create action should materialize output file");

        std::filesystem::remove(outputFile);

        std::cout << "integration_closed_loop_feedback: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "integration_closed_loop_feedback: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
