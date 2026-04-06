#include <iostream>
#include <set>
#include <string>

#include "EnvironmentAdapter.h"
#include "TestHelpers.h"

namespace {

iee::EnvironmentState BuildState() {
    iee::EnvironmentState state;
    state.sequence = 0;
    state.sourceSnapshotVersion = 0;
    state.valid = true;
    state.simulated = true;
    state.activeWindowTitle = L"ScreenPerceptionTest";
    state.activeProcessPath = L"screen_perception_test.exe";
    state.cursorPosition = {140, 90};

    iee::UiElement saveButton;
    saveButton.id = "ui-save";
    saveButton.name = L"Save";
    saveButton.controlType = iee::UiControlType::Button;
    saveButton.bounds = {80, 50, 220, 110};
    saveButton.isFocused = true;
    saveButton.isEnabled = true;

    iee::UiElement input;
    input.id = "ui-query";
    input.name = L"Query";
    input.controlType = iee::UiControlType::TextBox;
    input.bounds = {70, 130, 320, 190};
    input.isEnabled = true;

    state.uiElements.push_back(std::move(saveButton));
    state.uiElements.push_back(std::move(input));
    return state;
}

}  // namespace

int main() {
    try {
        iee::MockEnvironmentAdapter adapter({BuildState()});
        adapter.SetLooping(true);

        iee::EnvironmentState captured;
        AssertTrue(adapter.CaptureState(&captured), "Expected mock adapter capture to succeed");
        AssertTrue(captured.valid, "Captured state should be valid");
        AssertTrue(captured.screenFrame.valid, "Expected screen frame to be populated");
        AssertTrue(captured.screenFrame.frameId > 0, "Expected non-zero screen frame id");
        AssertTrue(captured.screenState.valid, "Expected unified screen state");
        AssertTrue(!captured.screenState.elements.empty(), "Expected merged screen elements");
        AssertTrue(!captured.screenState.visualElements.empty(), "Expected visual detections");
        AssertTrue(captured.screenState.signature != 0, "Expected non-zero screen signature");

        bool hasCursorElement = false;
        bool hasUiOrMergedElement = false;
        for (const iee::ScreenElement& element : captured.screenState.elements) {
            hasCursorElement = hasCursorElement || element.source == "cursor";
            hasUiOrMergedElement = hasUiOrMergedElement || element.source == "uia" || element.source == "merged";
        }
        AssertTrue(hasCursorElement, "Unified screen state should include cursor element");
        AssertTrue(hasUiOrMergedElement, "Unified screen state should include UI/merged elements");

        iee::EnvironmentState second;
        AssertTrue(adapter.CaptureState(&second), "Expected second capture to succeed");
        AssertTrue(second.screenState.frameId >= captured.screenState.frameId, "Expected monotonic frame ids");

        std::set<std::string> ids;
        for (const iee::ScreenElement& element : second.screenState.elements) {
            ids.insert(element.id);
        }
        AssertTrue(ids.size() == second.screenState.elements.size(), "Expected unique screen element ids");

        std::cout << "unit_screen_perception: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "unit_screen_perception: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
