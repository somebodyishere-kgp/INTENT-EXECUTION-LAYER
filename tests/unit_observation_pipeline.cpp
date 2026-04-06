#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "EnvironmentAdapter.h"
#include "TestHelpers.h"

namespace {

iee::EnvironmentState BuildState(std::uint64_t sequence, const std::wstring& title, iee::UiControlType controlType) {
    iee::EnvironmentState state;
    state.sequence = sequence;
    state.sourceSnapshotVersion = sequence;
    state.valid = true;
    state.simulated = true;
    state.activeWindowTitle = title;
    state.activeProcessPath = L"unit_observation_pipeline.exe";
    state.cursorPosition = {120, 40};

    iee::UiElement element;
    element.id = "element-" + std::to_string(sequence);
    element.name = L"Element";
    element.controlType = controlType;
    element.isFocused = true;
    element.bounds.left = 10;
    element.bounds.top = 10;
    element.bounds.right = 210;
    element.bounds.bottom = 80;
    state.uiElements.push_back(std::move(element));
    return state;
}

}  // namespace

int main() {
    try {
        auto adapter = std::make_shared<iee::MockEnvironmentAdapter>();
        adapter->SetLooping(true);
        adapter->SetScriptedStates({
            BuildState(1, L"StateA", iee::UiControlType::Button),
            BuildState(2, L"StateB", iee::UiControlType::TextBox),
            BuildState(3, L"StateC", iee::UiControlType::MenuItem)});

        iee::ObservationPipeline pipeline;
        iee::ObservationPipelineConfig config;
        config.sampleIntervalMs = 1;

        std::string message;
        AssertTrue(pipeline.Start(adapter, config, &message), "Observation pipeline should start");

        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        iee::EnvironmentState latest;
        AssertTrue(pipeline.Latest(&latest), "Expected latest environment state");
        AssertTrue(latest.valid, "Latest state should be valid");
        AssertTrue(latest.sequence >= 1, "Latest sequence should advance");
        AssertTrue(!latest.perception.dominantSurface.empty(), "Perception should be computed");

        const iee::ObservationPipelineMetrics metrics = pipeline.Metrics();
        AssertTrue(metrics.samples >= 1, "Expected observation pipeline to capture at least one sample");
        AssertTrue(metrics.captureFailures == 0, "Expected no observation capture failures");

        pipeline.Stop();
        AssertTrue(!pipeline.Running(), "Observation pipeline should stop cleanly");

        std::cout << "unit_observation_pipeline: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "unit_observation_pipeline: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
