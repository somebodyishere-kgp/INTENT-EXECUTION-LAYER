#include <iostream>
#include <vector>

#include "InteractionGraph.h"
#include "TestHelpers.h"

namespace {

std::vector<iee::UiElement> BuildSyntheticTree() {
    iee::UiElement root;
    root.id = "root-window";
    root.name = L"App";
    root.controlType = iee::UiControlType::Window;
    root.isEnabled = true;
    root.isVisible = true;
    root.isOffscreen = false;
    root.bounds = {0, 0, 1024, 768};

    iee::UiElement menu;
    menu.id = "menu-file";
    menu.parentId = root.id;
    menu.name = L"File";
    menu.controlType = iee::UiControlType::Menu;
    menu.isEnabled = true;
    menu.isVisible = true;
    menu.isCollapsed = true;
    menu.isHidden = true;
    menu.bounds = {0, 0, 120, 28};
    menu.acceleratorKey = L"Alt+F";

    iee::UiElement hiddenMenuItem;
    hiddenMenuItem.id = "menu-file-save";
    hiddenMenuItem.parentId = menu.id;
    hiddenMenuItem.name = L"Save As";
    hiddenMenuItem.controlType = iee::UiControlType::MenuItem;
    hiddenMenuItem.isEnabled = true;
    hiddenMenuItem.isVisible = false;
    hiddenMenuItem.isOffscreen = true;
    hiddenMenuItem.isHidden = true;
    hiddenMenuItem.bounds = {10, 35, 210, 65};

    iee::UiElement offscreenInput;
    offscreenInput.id = "input-hidden";
    offscreenInput.parentId = root.id;
    offscreenInput.name = L"Hidden Query";
    offscreenInput.controlType = iee::UiControlType::TextBox;
    offscreenInput.isEnabled = true;
    offscreenInput.isVisible = false;
    offscreenInput.isOffscreen = true;
    offscreenInput.isHidden = true;
    offscreenInput.bounds = {2000, 1200, 2200, 1260};

    return {root, menu, hiddenMenuItem, offscreenInput};
}

}  // namespace

int main() {
    try {
        const std::vector<iee::UiElement> elements = BuildSyntheticTree();
        std::vector<iee::UiElement> changedElements = elements;
        changedElements[2].isVisible = true;
        changedElements[2].isHidden = false;
        changedElements[2].isOffscreen = false;

        const iee::InteractionGraph graphA = iee::InteractionGraphBuilder::Build(elements, 1001);
        const iee::InteractionGraph graphB = iee::InteractionGraphBuilder::Build(elements, 1002);
        const iee::InteractionGraph graphC = iee::InteractionGraphBuilder::Build(changedElements, 1003);

        AssertTrue(graphA.valid, "Expected graph to be valid");
        AssertTrue(graphA.nodes.size() >= elements.size(), "Expected graph nodes to include all UI elements");
        AssertTrue(graphA.signature == graphB.signature, "Expected deterministic graph signatures across frames");
        AssertTrue(graphA.version == 1001, "Expected graph version to track sequence");

        const auto findByUiId = [](const iee::InteractionGraph& graph, const std::string& uiElementId) {
            for (const auto& entry : graph.nodes) {
                if (entry.second.uiElementId == uiElementId) {
                    return std::optional<iee::InteractionNode>(entry.second);
                }
            }
            return std::optional<iee::InteractionNode>{};
        };

        const auto menuNode = findByUiId(graphA, "menu-file");
        AssertTrue(menuNode.has_value(), "Expected menu node to exist");
        AssertTrue(menuNode->collapsed, "Expected collapsed menu node");
        AssertTrue(menuNode->hidden, "Expected collapsed menu to be marked hidden");
        AssertTrue(menuNode->revealStrategy.required, "Collapsed menu should require reveal strategy");
        AssertTrue(!menuNode->executionPlan.steps.empty(), "Every node should expose execution plan steps");

        const auto hiddenInputNode = findByUiId(graphA, "input-hidden");
        AssertTrue(hiddenInputNode.has_value(), "Expected hidden textbox node to exist");
        AssertTrue(hiddenInputNode->offscreen, "Expected offscreen node to be marked offscreen");
        AssertTrue(hiddenInputNode->hidden, "Expected offscreen node to be marked hidden");
        AssertTrue(hiddenInputNode->intentBinding.action == iee::IntentAction::SetValue, "Hidden textbox should map to set_value action");

        const auto menuNodeB = findByUiId(graphB, "menu-file");
        AssertTrue(menuNodeB.has_value(), "Expected menu node to exist on next frame");
        AssertTrue(menuNode->id == menuNodeB->id, "Expected stable node id across equivalent frames");
        AssertTrue(menuNode->nodeId.signature == menuNodeB->nodeId.signature, "Expected stable identity signature across equivalent frames");

        bool foundShortcutCommand = false;
        for (const iee::CommandNode& command : graphA.commands) {
            if (command.shortcut == "Alt+F") {
                foundShortcutCommand = true;
                const auto commandNode = iee::InteractionGraphBuilder::FindNode(graphA, command.id);
                AssertTrue(commandNode.has_value(), "Expected command node to exist for extracted shortcut");

                const iee::Intent commandIntent = iee::InteractionGraphBuilder::GenerateIntent(*commandNode);
                AssertTrue(commandIntent.action == iee::IntentAction::Activate, "Command node should map to activate intent");
                AssertTrue(commandIntent.target.nodeId == command.id, "Generated intent should target command node id");
                AssertTrue(commandIntent.params.Has("shortcut"), "Generated command intent should include shortcut parameter");
                AssertTrue(commandNode->executionPlan.executable, "Command node plan should be executable");
                break;
            }
        }
        AssertTrue(foundShortcutCommand, "Expected latent command extraction from accelerator keys");

        const std::vector<iee::Intent> visibleOnly = iee::InteractionGraphBuilder::GenerateIntents(graphA, false, 1024);
        for (const iee::Intent& intent : visibleOnly) {
            const auto node = iee::InteractionGraphBuilder::FindNode(graphA, intent.target.nodeId);
            AssertTrue(node.has_value(), "Visible intent should map to graph node");
            AssertTrue(!node->hidden, "Visible-only intents should not include hidden nodes");
        }

        const std::vector<iee::Intent> includeHidden = iee::InteractionGraphBuilder::GenerateIntents(graphA, true, 1024);
        AssertTrue(includeHidden.size() >= visibleOnly.size(), "Hidden-inclusive intent list should be at least visible-only size");

        const iee::GraphDelta stableDelta = iee::InteractionGraphBuilder::ComputeDelta(graphA, graphB);
        AssertTrue(!stableDelta.changed, "Equivalent frames should produce empty graph deltas");

        const iee::GraphDelta changedDelta = iee::InteractionGraphBuilder::ComputeDelta(graphA, graphC);
        AssertTrue(changedDelta.changed, "Graph delta should flag changed frames");
        AssertTrue(!changedDelta.updatedNodes.empty() || !changedDelta.addedNodes.empty(), "Changed graph should include updated or added nodes");

        std::cout << "unit_interaction_graph: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "unit_interaction_graph: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
