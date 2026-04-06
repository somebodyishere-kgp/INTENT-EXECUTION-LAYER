#include <iostream>
#include <memory>

#include "Adapter.h"
#include "CapabilityGraph.h"
#include "EventBus.h"
#include "IntentRegistry.h"
#include "IntentResolver.h"
#include "Telemetry.h"
#include "TestHelpers.h"

namespace {

class FakeObserver final : public iee::IObserverEngine {
public:
    iee::ObserverSnapshot Capture() override {
        iee::ObserverSnapshot snapshot;
        snapshot.valid = true;
        snapshot.sequence = 1;
        snapshot.cursorPosition = {100, 100};
        snapshot.activeWindowTitle = L"FakeApp";
        snapshot.activeProcessPath = L"fake.exe";
        return snapshot;
    }
};

class DeterministicUiAdapter final : public iee::Adapter {
public:
    std::string Name() const override {
        return "UIAAdapter";
    }

    std::vector<iee::Intent> GetCapabilities(const iee::ObserverSnapshot& snapshot, const iee::CapabilityGraph&) override {
        std::vector<iee::Intent> intents;

        iee::Intent focusedSave;
        focusedSave.id = "save-focused";
        focusedSave.name = "activate";
        focusedSave.action = iee::IntentAction::Activate;
        focusedSave.source = "uia";
        focusedSave.confidence = 0.95F;
        focusedSave.target.type = iee::TargetType::UiElement;
        focusedSave.target.label = L"Save";
        focusedSave.target.hierarchyDepth = 2;
        focusedSave.target.focused = true;
        focusedSave.target.screenCenter = {105, 102};
        focusedSave.context.cursor = snapshot.cursorPosition;
        intents.push_back(focusedSave);

        iee::Intent farSave;
        farSave.id = "save-far";
        farSave.name = "activate";
        farSave.action = iee::IntentAction::Activate;
        farSave.source = "uia";
        farSave.confidence = 0.95F;
        farSave.target.type = iee::TargetType::UiElement;
        farSave.target.label = L"Save";
        farSave.target.hierarchyDepth = 6;
        farSave.target.focused = false;
        farSave.target.screenCenter = {900, 600};
        farSave.context.cursor = snapshot.cursorPosition;
        intents.push_back(farSave);

        return intents;
    }

    bool CanExecute(const iee::Intent&) const override {
        return false;
    }

    iee::ExecutionResult Execute(const iee::Intent&) override {
        return {};
    }
};

}  // namespace

int main() {
    try {
        iee::EventBus eventBus;
        iee::AdapterRegistry adapters;
        adapters.Register(std::make_unique<DeterministicUiAdapter>());

        FakeObserver observer;
        iee::CapabilityGraphBuilder graphBuilder;
        iee::IntentResolver resolver;
        iee::Telemetry telemetry;

        iee::IntentRegistry registry(observer, adapters, graphBuilder, resolver, eventBus, telemetry);
        registry.Refresh();

        const iee::ResolutionResult resolved = registry.Resolve(iee::IntentAction::Activate, L"Save");
        AssertTrue(resolved.bestMatch.has_value(), "Expected a best match");
        AssertTrue(resolved.bestMatch->intent.id == "save-focused", "Expected focused Save to win deterministic ranking");
        AssertTrue(!resolved.ambiguity.has_value(), "No ambiguity expected in deterministic scenario");

        std::cout << "scenario_resolution: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "scenario_resolution: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
