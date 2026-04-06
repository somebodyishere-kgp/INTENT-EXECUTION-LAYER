#include <chrono>
#include <iostream>
#include <set>
#include <thread>

#include "EnvironmentAdapter.h"
#include "TestHelpers.h"

namespace {

iee::EnvironmentState BuildState(const std::wstring& title, int xOffset) {
    iee::EnvironmentState state;
    state.sequence = 0;
    state.sourceSnapshotVersion = 0;
    state.valid = true;
    state.simulated = true;
    state.activeWindowTitle = title;
    state.activeProcessPath = L"stress_screen_pipeline.exe";
    state.cursorPosition = {140 + xOffset, 96};

    iee::UiElement button;
    button.id = "action-button";
    button.name = L"Run";
    button.controlType = iee::UiControlType::Button;
    button.bounds = {40 + xOffset, 40, 180 + xOffset, 90};
    button.isFocused = true;

    iee::UiElement textbox;
    textbox.id = "query-box";
    textbox.name = L"Query";
    textbox.controlType = iee::UiControlType::TextBox;
    textbox.bounds = {40 + xOffset, 120, 320 + xOffset, 180};

    state.uiElements.push_back(std::move(button));
    state.uiElements.push_back(std::move(textbox));
    return state;
}

}  // namespace

int main() {
    try {
        auto adapter = std::make_shared<iee::MockEnvironmentAdapter>();
        adapter->SetScriptedStates({
            BuildState(L"ScenarioA", 0),
            BuildState(L"ScenarioB", 36)});
        adapter->SetLooping(true);

        iee::ObservationPipeline pipeline;
        iee::ObservationPipelineConfig config;
        config.sampleIntervalMs = 1;

        std::string message;
        AssertTrue(pipeline.Start(adapter, config, &message), "Observation pipeline should start");

        std::set<std::uint64_t> signatures;
        const auto observeUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(420);
        while (std::chrono::steady_clock::now() < observeUntil) {
            iee::EnvironmentState state;
            if (pipeline.Latest(&state) && state.screenState.valid) {
                signatures.insert(state.screenState.signature);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        const iee::ObservationPipelineMetrics metrics = pipeline.Metrics();
        pipeline.Stop();

        AssertTrue(metrics.samples >= 20, "Expected observation pipeline to produce sustained samples");
        AssertTrue(metrics.latestFrameId > 0, "Expected non-zero latest frame id");
        AssertTrue(metrics.averageVisionCaptureMs >= 0.0, "Vision capture metric should be available");
        AssertTrue(metrics.averageVisionDetectionMs >= 0.0, "Vision detection metric should be available");
        AssertTrue(metrics.averageVisionMergeMs >= 0.0, "Vision merge metric should be available");
        AssertTrue(metrics.estimatedFps >= 30.0, "Expected observation pipeline to sustain at least 30 FPS");
        AssertTrue(signatures.size() >= 2, "Expected dynamic screen signatures across scripted scenarios");

        std::cout << "stress_screen_pipeline: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "stress_screen_pipeline: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
