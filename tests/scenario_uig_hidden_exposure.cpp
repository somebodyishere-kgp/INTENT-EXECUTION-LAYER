#include <iostream>
#include <memory>
#include <optional>

#include "EnvironmentAdapter.h"
#include "TestHelpers.h"

namespace {

iee::EnvironmentState BuildState(std::uint64_t sequence) {
    iee::EnvironmentState state;
    state.sequence = sequence;
    state.sourceSnapshotVersion = sequence;
    state.valid = true;
    state.simulated = true;
    state.activeWindowTitle = L"UIG Scenario";
    state.activeProcessPath = L"scenario_uig.exe";
    state.cursorPosition = {64, 48};

    iee::UiElement visibleButton;
    visibleButton.id = "save-button";
    visibleButton.name = L"Save";
    visibleButton.controlType = iee::UiControlType::Button;
    visibleButton.isEnabled = true;
    visibleButton.isVisible = true;
    visibleButton.isOffscreen = false;
    visibleButton.bounds = {40, 20, 200, 80};

    iee::UiElement hiddenMenu;
    hiddenMenu.id = "hidden-menu";
    hiddenMenu.parentId = "save-button";
    hiddenMenu.name = L"Hidden Export";
    hiddenMenu.controlType = iee::UiControlType::MenuItem;
    hiddenMenu.isEnabled = true;
    hiddenMenu.isVisible = false;
    hiddenMenu.isOffscreen = true;
    hiddenMenu.isHidden = true;
    hiddenMenu.isCollapsed = true;
    hiddenMenu.acceleratorKey = L"Ctrl+E";
    hiddenMenu.bounds = {220, 100, 420, 130};

    state.uiElements.push_back(std::move(visibleButton));
    state.uiElements.push_back(std::move(hiddenMenu));
    return state;
}

}  // namespace

int main() {
    try {
        auto adapter = std::make_shared<iee::MockEnvironmentAdapter>(
            std::vector<iee::EnvironmentState>{BuildState(1), BuildState(2)});
        adapter->SetLooping(false);

        iee::EnvironmentState first;
        AssertTrue(adapter->CaptureState(&first), "First capture should succeed");
        AssertTrue(first.unifiedState.valid, "Unified state should be populated");
        AssertTrue(first.unifiedState.screenState.valid, "Unified state should embed a valid screen state");
        AssertTrue(first.unifiedState.interactionGraph.valid, "Unified state should embed a valid interaction graph");

        std::optional<iee::InteractionNode> hiddenNode;
        for (const auto& entry : first.unifiedState.interactionGraph.nodes) {
            if (entry.second.uiElementId == "hidden-menu") {
                hiddenNode = entry.second;
                break;
            }
        }
        AssertTrue(hiddenNode.has_value(), "Expected hidden interaction node in unified state");
        AssertTrue(hiddenNode->hidden, "Hidden node should remain explicitly marked hidden");
        AssertTrue(hiddenNode->offscreen, "Hidden node should preserve offscreen state");
        AssertTrue(hiddenNode->revealStrategy.required, "Hidden node should expose a reveal strategy");

        iee::EnvironmentState second;
        AssertTrue(adapter->CaptureState(&second), "Second capture should succeed");
        AssertTrue(second.unifiedState.valid, "Unified state should remain valid on next frame");
        AssertTrue(
            second.unifiedState.interactionGraph.signature == first.unifiedState.interactionGraph.signature,
            "Expected interaction graph signature stability across equivalent frames");

        std::cout << "scenario_uig_hidden_exposure: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "scenario_uig_hidden_exposure: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
