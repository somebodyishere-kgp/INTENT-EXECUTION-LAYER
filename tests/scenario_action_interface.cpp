#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>

#include "Adapter.h"
#include "CapabilityGraph.h"
#include "EventBus.h"
#include "ExecutionEngine.h"
#include "IntentApiServer.h"
#include "IntentRegistry.h"
#include "IntentResolver.h"
#include "ObserverEngine.h"
#include "Telemetry.h"
#include "TestHelpers.h"

namespace {

class ScenarioObserver final : public iee::IObserverEngine {
public:
    iee::ObserverSnapshot Capture() override {
        iee::ObserverSnapshot snapshot;
        snapshot.valid = true;
        snapshot.sequence = ++sequence_;
        snapshot.activeWindowTitle = L"Scenario Workspace";
        snapshot.activeProcessPath = L"C:\\Program Files\\App\\app.exe";
        snapshot.cursorPosition = {96, 96};

        iee::UiElement root;
        root.id = "root";
        root.name = L"Root";
        root.controlType = iee::UiControlType::Window;
        root.isEnabled = true;
        root.isVisible = true;
        root.isOffscreen = false;
        root.bounds = {0, 0, 1600, 900};

        iee::UiElement commandPalette;
        commandPalette.id = "vscode-command-palette";
        commandPalette.parentId = root.id;
        commandPalette.name = L"Command Palette";
        commandPalette.controlType = iee::UiControlType::Button;
        commandPalette.isEnabled = true;
        commandPalette.isVisible = true;
        commandPalette.isOffscreen = false;
        commandPalette.bounds = {24, 24, 280, 64};

        iee::UiElement browserInput;
        browserInput.id = "browser-address";
        browserInput.parentId = root.id;
        browserInput.name = L"Search Bar";
        browserInput.controlType = iee::UiControlType::TextBox;
        browserInput.isEnabled = true;
        browserInput.isVisible = true;
        browserInput.isOffscreen = false;
        browserInput.supportsValue = true;
        browserInput.bounds = {24, 96, 560, 140};

        iee::UiElement browserGo;
        browserGo.id = "browser-go";
        browserGo.parentId = root.id;
        browserGo.name = L"Go";
        browserGo.controlType = iee::UiControlType::Button;
        browserGo.isEnabled = true;
        browserGo.isVisible = true;
        browserGo.isOffscreen = false;
        browserGo.bounds = {580, 96, 640, 140};

        iee::UiElement fileMenu;
        fileMenu.id = "ppt-file-menu";
        fileMenu.parentId = root.id;
        fileMenu.name = L"File Menu";
        fileMenu.controlType = iee::UiControlType::Menu;
        fileMenu.isEnabled = true;
        fileMenu.isVisible = true;
        fileMenu.isOffscreen = false;
        fileMenu.bounds = {24, 164, 180, 204};

        iee::UiElement exportSlideDeck;
        exportSlideDeck.id = "ppt-export-slide-deck";
        exportSlideDeck.parentId = fileMenu.id;
        exportSlideDeck.name = L"Export Slide Deck";
        exportSlideDeck.controlType = iee::UiControlType::MenuItem;
        exportSlideDeck.isEnabled = true;
        exportSlideDeck.isVisible = hiddenMenuExposed_;
        exportSlideDeck.isOffscreen = !hiddenMenuExposed_;
        exportSlideDeck.isHidden = !hiddenMenuExposed_;
        exportSlideDeck.isCollapsed = !hiddenMenuExposed_;
        exportSlideDeck.bounds = {24, 212, 320, 252};

        snapshot.uiElements = {
            root,
            commandPalette,
            browserInput,
            browserGo,
            fileMenu,
            exportSlideDeck};

        return snapshot;
    }

    void ExposeHiddenMenu() {
        hiddenMenuExposed_ = true;
    }

private:
    std::uint64_t sequence_{0};
    bool hiddenMenuExposed_{false};
};

class ScenarioUiAdapter final : public iee::Adapter {
public:
    explicit ScenarioUiAdapter(ScenarioObserver& observer)
        : observer_(observer) {}

    std::string Name() const override {
        return "UIAAdapter";
    }

    std::vector<iee::Intent> GetCapabilities(const iee::ObserverSnapshot& snapshot, const iee::CapabilityGraph&) override {
        std::vector<iee::Intent> intents;
        for (const iee::UiElement& element : snapshot.uiElements) {
            if (element.controlType == iee::UiControlType::Window) {
                continue;
            }

            iee::Intent base;
            base.id = "base:" + element.id;
            base.name = "activate";
            base.action = iee::IntentAction::Activate;
            base.source = "uia";
            base.confidence = 1.0F;
            base.target.type = iee::TargetType::UiElement;
            base.target.nodeId = element.id;
            base.target.label = element.name;
            base.target.automationId = element.automationId;
            base.target.hierarchyDepth = element.depth;
            base.target.focused = element.isFocused;
            base.target.screenCenter = {
                element.bounds.left + ((element.bounds.right - element.bounds.left) / 2),
                element.bounds.top + ((element.bounds.bottom - element.bounds.top) / 2)};
            intents.push_back(base);

            iee::Intent selectIntent = base;
            selectIntent.id = "select:" + element.id;
            selectIntent.name = "select";
            selectIntent.action = iee::IntentAction::Select;
            intents.push_back(selectIntent);

            if (element.controlType == iee::UiControlType::TextBox || element.supportsValue) {
                iee::Intent setValue = base;
                setValue.id = "set:" + element.id;
                setValue.name = "set_value";
                setValue.action = iee::IntentAction::SetValue;
                setValue.params.values["value"] = L"";
                intents.push_back(setValue);
            }
        }

        return intents;
    }

    bool CanExecute(const iee::Intent& intent) const override {
        return intent.target.type == iee::TargetType::UiElement &&
            (intent.action == iee::IntentAction::Activate ||
             intent.action == iee::IntentAction::Select ||
             intent.action == iee::IntentAction::SetValue);
    }

    iee::ExecutionResult Execute(const iee::Intent& intent) override {
        iee::ExecutionResult result;
        result.method = "uia-scenario";
        result.duration = std::chrono::milliseconds(2);

        if (intent.params.Has("reveal_action")) {
            observer_.ExposeHiddenMenu();
        }

        if (intent.action == iee::IntentAction::SetValue && intent.params.Get("value").empty()) {
            result.status = iee::ExecutionStatus::FAILED;
            result.verified = false;
            result.message = "value missing";
            return result;
        }

        result.status = iee::ExecutionStatus::SUCCESS;
        result.verified = true;
        result.message = "ok";
        return result;
    }

    iee::AdapterScore GetScore() const override {
        iee::AdapterScore score;
        score.reliability = 0.97F;
        score.latency = 12.0F;
        score.confidence = 0.97F;
        return score;
    }

private:
    ScenarioObserver& observer_;
};

std::string BuildHttpRequest(const std::string& method, const std::string& path, const std::string& body = "") {
    std::ostringstream stream;
    stream << method << " " << path << " HTTP/1.1\r\n";
    stream << "Host: 127.0.0.1\r\n";
    stream << "Content-Type: application/json\r\n";
    stream << "Content-Length: " << body.size() << "\r\n\r\n";
    stream << body;
    return stream.str();
}

bool HasStatus(const std::string& response, int code) {
    const std::string marker = "HTTP/1.1 " + std::to_string(code);
    return response.find(marker) != std::string::npos;
}

}  // namespace

int main() {
    try {
        iee::EventBus eventBus;
        ScenarioObserver observer;

        iee::AdapterRegistry adapters;
        adapters.RegisterAdapter(std::make_shared<ScenarioUiAdapter>(observer));

        iee::CapabilityGraphBuilder graphBuilder;
        iee::IntentResolver resolver;
        iee::Telemetry telemetry;
        iee::IntentRegistry registry(observer, adapters, graphBuilder, resolver, eventBus, telemetry);
        iee::IntentValidator validator;
        iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

        iee::IntentApiServer api(registry, engine, telemetry);

        {
            const std::string body =
                "{\"action\":\"open\",\"target\":\"Command Palette\",\"context\":{\"app\":\"code\",\"domain\":\"ide\"}}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/act", body));
            AssertTrue(HasStatus(response, 200), "Scenario: VS Code command palette action should succeed");
            AssertTrue(response.find("\"status\":\"success\"") != std::string::npos, "Scenario: command palette should return success");
        }

        {
            const std::string setBody =
                "{\"action\":\"set_value\",\"target\":\"Search Bar\",\"value\":\"github copilot\",\"context\":{\"app\":\"browser\",\"domain\":\"browser\"}}";
            const std::string setResponse = api.HandleRequestForTesting(BuildHttpRequest("POST", "/act", setBody));
            AssertTrue(HasStatus(setResponse, 200), "Scenario: browser input step should succeed");
            AssertTrue(setResponse.find("\"status\":\"success\"") != std::string::npos, "Scenario: browser input should return success");

            const std::string clickBody =
                "{\"action\":\"activate\",\"target\":\"Go\",\"context\":{\"app\":\"browser\",\"domain\":\"browser\"}}";
            const std::string clickResponse = api.HandleRequestForTesting(BuildHttpRequest("POST", "/act", clickBody));
            AssertTrue(HasStatus(clickResponse, 200), "Scenario: browser click step should succeed");
            AssertTrue(clickResponse.find("\"status\":\"success\"") != std::string::npos, "Scenario: browser click should return success");
        }

        {
            const std::string body =
                "{\"action\":\"select\",\"target\":\"Export Slide Deck\",\"context\":{\"app\":\"powerpoint\",\"domain\":\"presentation\"}}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/act", body));
            AssertTrue(HasStatus(response, 200), "Scenario: hidden menu action should succeed");
            AssertTrue(response.find("\"status\":\"success\"") != std::string::npos, "Scenario: hidden menu should return success");
            AssertTrue(response.find("\"reveal_used\":true") != std::string::npos, "Scenario: hidden menu should use reveal");
        }

        std::cout << "scenario_action_interface: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "scenario_action_interface: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
