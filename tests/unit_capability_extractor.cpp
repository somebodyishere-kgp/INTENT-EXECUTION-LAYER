#include <iostream>

#include "CapabilityExtractor.h"
#include "TestHelpers.h"

int main() {
    try {
        iee::ObserverSnapshot snapshot;
        snapshot.valid = true;

        iee::UiElement button;
        button.id = "button-save";
        button.name = L"Save";
        button.controlType = iee::UiControlType::Button;
        button.supportsInvoke = true;
        snapshot.uiElements.push_back(button);

        iee::UiElement textbox;
        textbox.id = "textbox-title";
        textbox.name = L"Title";
        textbox.controlType = iee::UiControlType::TextBox;
        textbox.supportsValue = true;
        snapshot.uiElements.push_back(textbox);

        iee::UiElement menu;
        menu.id = "menu-file";
        menu.name = L"File";
        menu.controlType = iee::UiControlType::MenuItem;
        menu.supportsSelection = true;
        snapshot.uiElements.push_back(menu);

        iee::FileSystemEntry fileEntry;
        fileEntry.path = L"unit_test.txt";
        fileEntry.isDirectory = false;
        snapshot.fileSystemEntries.push_back(fileEntry);

        iee::CapabilityExtractor extractor;
        const auto capabilities = extractor.Extract(snapshot);

        bool hasActivate = false;
        bool hasSetValue = false;
        bool hasSelect = false;
        bool hasCreate = false;
        bool hasDelete = false;
        bool hasMove = false;

        for (const auto& capability : capabilities) {
            const auto action = iee::ToString(capability.action);
            hasActivate = hasActivate || action == "activate";
            hasSetValue = hasSetValue || action == "set_value";
            hasSelect = hasSelect || action == "select";
            hasCreate = hasCreate || action == "create";
            hasDelete = hasDelete || action == "delete";
            hasMove = hasMove || action == "move";
        }

        AssertTrue(hasActivate, "Expected activate capability");
        AssertTrue(hasSetValue, "Expected set_value capability");
        AssertTrue(hasSelect, "Expected select capability");
        AssertTrue(hasCreate, "Expected create capability");
        AssertTrue(hasDelete, "Expected delete capability");
        AssertTrue(hasMove, "Expected move capability");

        std::cout << "unit_capability_extractor: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "unit_capability_extractor: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
