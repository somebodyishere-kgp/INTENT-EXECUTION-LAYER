#include <iostream>
#include <optional>
#include <vector>

#include "InteractionGraph.h"
#include "TaskInterface.h"
#include "TestHelpers.h"

namespace {

std::vector<iee::UiElement> BuildScenarioTree() {
    iee::UiElement root;
    root.id = "root-window";
    root.name = L"Scenario App";
    root.controlType = iee::UiControlType::Window;
    root.isEnabled = true;
    root.isVisible = true;
    root.isOffscreen = false;
    root.bounds = {0, 0, 1280, 720};

    iee::UiElement presentationButton;
    presentationButton.id = "presentation-start";
    presentationButton.parentId = root.id;
    presentationButton.name = L"Start Presentation";
    presentationButton.controlType = iee::UiControlType::Button;
    presentationButton.isEnabled = true;
    presentationButton.isVisible = true;
    presentationButton.isOffscreen = false;
    presentationButton.bounds = {40, 40, 320, 96};

    iee::UiElement addressBar;
    addressBar.id = "browser-address";
    addressBar.parentId = root.id;
    addressBar.name = L"Address Bar";
    addressBar.controlType = iee::UiControlType::TextBox;
    addressBar.isEnabled = true;
    addressBar.isVisible = true;
    addressBar.isOffscreen = false;
    addressBar.bounds = {40, 140, 800, 188};

    iee::UiElement hiddenMenu;
    hiddenMenu.id = "menu-export";
    hiddenMenu.parentId = root.id;
    hiddenMenu.name = L"Export Slide Deck";
    hiddenMenu.controlType = iee::UiControlType::MenuItem;
    hiddenMenu.isEnabled = true;
    hiddenMenu.isVisible = false;
    hiddenMenu.isOffscreen = true;
    hiddenMenu.isHidden = true;
    hiddenMenu.isCollapsed = true;
    hiddenMenu.bounds = {20, 220, 340, 260};

    return {root, presentationButton, addressBar, hiddenMenu};
}

std::optional<iee::TaskPlanCandidate> FindCandidateById(
    const iee::TaskPlanResult& plan,
    const std::string& nodeId) {
    for (const auto& candidate : plan.candidates) {
        if (candidate.nodeId == nodeId) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace

int main() {
    try {
        const iee::InteractionGraph graph = iee::InteractionGraphBuilder::Build(BuildScenarioTree(), 2201);
        AssertTrue(graph.valid, "Scenario graph should be valid");

        std::string presentationNodeId;
        std::string browserNodeId;
        std::string hiddenMenuNodeId;

        for (const auto& entry : graph.nodes) {
            if (entry.second.uiElementId == "presentation-start") {
                presentationNodeId = entry.second.id;
            } else if (entry.second.uiElementId == "browser-address") {
                browserNodeId = entry.second.id;
            } else if (entry.second.uiElementId == "menu-export") {
                hiddenMenuNodeId = entry.second.id;
            }
        }

        AssertTrue(!presentationNodeId.empty(), "Presentation node id should be discovered");
        AssertTrue(!browserNodeId.empty(), "Browser node id should be discovered");
        AssertTrue(!hiddenMenuNodeId.empty(), "Hidden menu node id should be discovered");

        iee::TaskPlanner planner;

        iee::TaskRequest presentationRequest;
        presentationRequest.goal = "start presentation";
        presentationRequest.targetHint = "presentation slide show";
        presentationRequest.domain = iee::TaskDomain::Presentation;
        presentationRequest.allowHidden = true;
        presentationRequest.maxPlans = 4;

        const iee::TaskPlanResult presentationPlan = planner.Plan(presentationRequest, graph);
        AssertTrue(!presentationPlan.candidates.empty(), "Presentation plan should produce candidates");
        const auto presentationCandidate = FindCandidateById(presentationPlan, presentationNodeId);
        AssertTrue(presentationCandidate.has_value(), "Presentation scenario should include presentation node candidate");

        iee::TaskRequest browserRequest;
        browserRequest.goal = "focus browser address input";
        browserRequest.targetHint = "address bar url";
        browserRequest.domain = iee::TaskDomain::Browser;
        browserRequest.allowHidden = true;
        browserRequest.maxPlans = 4;

        const iee::TaskPlanResult browserPlan = planner.Plan(browserRequest, graph);
        AssertTrue(!browserPlan.candidates.empty(), "Browser plan should produce candidates");
        const auto browserCandidate = FindCandidateById(browserPlan, browserNodeId);
        AssertTrue(browserCandidate.has_value(), "Browser scenario should include address-bar candidate");
        AssertTrue(
            browserCandidate->action == "set_value" || browserCandidate->action == "activate",
            "Browser candidate should remain executable through deterministic action mapping");

        iee::TaskRequest hiddenRequest;
        hiddenRequest.goal = "export deck";
        hiddenRequest.targetHint = "export";
        hiddenRequest.domain = iee::TaskDomain::Presentation;
        hiddenRequest.allowHidden = true;
        hiddenRequest.maxPlans = 6;

        const iee::TaskPlanResult hiddenPlan = planner.Plan(hiddenRequest, graph);
        AssertTrue(!hiddenPlan.candidates.empty(), "Hidden menu plan should produce candidates");
        const auto hiddenCandidate = FindCandidateById(hiddenPlan, hiddenMenuNodeId);
        AssertTrue(hiddenCandidate.has_value(), "Hidden menu scenario should include hidden-menu candidate");
        AssertTrue(hiddenCandidate->requiresReveal, "Hidden menu candidate should require reveal");

        std::cout << "scenario_task_interface: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "scenario_task_interface: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
