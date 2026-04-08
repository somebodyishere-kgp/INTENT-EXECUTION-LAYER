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

class ActionTestObserver final : public iee::IObserverEngine {
public:
    iee::ObserverSnapshot Capture() override {
        iee::ObserverSnapshot snapshot;
        snapshot.valid = true;
        snapshot.sequence = ++sequence_;
        snapshot.activeWindowTitle = L"Visual Studio Code";
        snapshot.activeProcessPath = L"C:\\Program Files\\Microsoft VS Code\\Code.exe";
        snapshot.cursorPosition = {120, 120};

        iee::UiElement root;
        root.id = "root-window";
        root.name = L"Root";
        root.controlType = iee::UiControlType::Window;
        root.isEnabled = true;
        root.isVisible = true;
        root.isOffscreen = false;
        root.bounds = {0, 0, 1280, 720};

        iee::UiElement commandPalette;
        commandPalette.id = "cmd-palette";
        commandPalette.parentId = root.id;
        commandPalette.name = L"Command Palette";
        commandPalette.controlType = iee::UiControlType::Button;
        commandPalette.isEnabled = true;
        commandPalette.isVisible = true;
        commandPalette.isOffscreen = false;
        commandPalette.bounds = {20, 20, 220, 70};

        iee::UiElement searchBar;
        searchBar.id = "search-bar";
        searchBar.parentId = root.id;
        searchBar.name = L"Search Bar";
        searchBar.controlType = iee::UiControlType::TextBox;
        searchBar.isEnabled = true;
        searchBar.isVisible = true;
        searchBar.isOffscreen = false;
        searchBar.supportsValue = true;
        searchBar.bounds = {20, 90, 480, 130};

        iee::UiElement goButton;
        goButton.id = "go-button";
        goButton.parentId = root.id;
        goButton.name = L"Go";
        goButton.controlType = iee::UiControlType::Button;
        goButton.isEnabled = true;
        goButton.isVisible = true;
        goButton.isOffscreen = false;
        goButton.bounds = {500, 90, 560, 130};

        iee::UiElement fileMenu;
        fileMenu.id = "file-menu";
        fileMenu.parentId = root.id;
        fileMenu.name = L"File Menu";
        fileMenu.controlType = iee::UiControlType::Menu;
        fileMenu.isEnabled = true;
        fileMenu.isVisible = true;
        fileMenu.isOffscreen = false;
        fileMenu.bounds = {20, 150, 160, 190};

        iee::UiElement hiddenExport;
        hiddenExport.id = "hidden-export";
        hiddenExport.parentId = fileMenu.id;
        hiddenExport.name = L"Export Slide Deck";
        hiddenExport.controlType = iee::UiControlType::MenuItem;
        hiddenExport.isEnabled = true;
        hiddenExport.isVisible = hiddenRevealed_;
        hiddenExport.isOffscreen = !hiddenRevealed_;
        hiddenExport.isHidden = !hiddenRevealed_;
        hiddenExport.isCollapsed = !hiddenRevealed_;
        hiddenExport.bounds = {20, 200, 300, 240};

        iee::UiElement ambiguousA;
        ambiguousA.id = "ambiguous-a";
        ambiguousA.parentId = root.id;
        ambiguousA.name = L"Common Target";
        ambiguousA.controlType = iee::UiControlType::Button;
        ambiguousA.isEnabled = true;
        ambiguousA.isVisible = true;
        ambiguousA.isOffscreen = false;
        ambiguousA.bounds = {20, 260, 220, 300};

        iee::UiElement ambiguousB;
        ambiguousB.id = "ambiguous-b";
        ambiguousB.parentId = root.id;
        ambiguousB.name = L"Common Target";
        ambiguousB.controlType = iee::UiControlType::Button;
        ambiguousB.isEnabled = true;
        ambiguousB.isVisible = true;
        ambiguousB.isOffscreen = false;
        ambiguousB.bounds = {260, 260, 460, 300};

        snapshot.uiElements = {
            root,
            commandPalette,
            searchBar,
            goButton,
            fileMenu,
            hiddenExport,
            ambiguousA,
            ambiguousB};

        return snapshot;
    }

    void RevealHidden() {
        hiddenRevealed_ = true;
    }

private:
    std::uint64_t sequence_{0};
    bool hiddenRevealed_{false};
};

class ActionTestUiAdapter final : public iee::Adapter {
public:
    explicit ActionTestUiAdapter(ActionTestObserver& observer)
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

            iee::Intent activate;
            activate.id = "act:" + element.id;
            activate.name = "activate";
            activate.action = iee::IntentAction::Activate;
            activate.source = "uia";
            activate.confidence = 1.0F;
            activate.target.type = iee::TargetType::UiElement;
            activate.target.label = element.name;
            activate.target.nodeId = element.id;
            activate.target.automationId = element.automationId;
            activate.target.hierarchyDepth = element.depth;
            activate.target.focused = element.isFocused;
            activate.target.screenCenter = {
                element.bounds.left + ((element.bounds.right - element.bounds.left) / 2),
                element.bounds.top + ((element.bounds.bottom - element.bounds.top) / 2)};
            intents.push_back(activate);

            if (element.controlType == iee::UiControlType::TextBox || element.supportsValue) {
                iee::Intent setValue = activate;
                setValue.id = "set:" + element.id;
                setValue.name = "set_value";
                setValue.action = iee::IntentAction::SetValue;
                setValue.params.values["value"] = L"";
                intents.push_back(setValue);
            }

            if (element.controlType == iee::UiControlType::Menu ||
                element.controlType == iee::UiControlType::MenuItem ||
                element.controlType == iee::UiControlType::Button) {
                iee::Intent selectIntent = activate;
                selectIntent.id = "sel:" + element.id;
                selectIntent.name = "select";
                selectIntent.action = iee::IntentAction::Select;
                intents.push_back(selectIntent);
            }
        }

        return intents;
    }

    bool CanExecute(const iee::Intent& intent) const override {
        return intent.target.type == iee::TargetType::UiElement &&
            (intent.action == iee::IntentAction::Activate ||
             intent.action == iee::IntentAction::SetValue ||
             intent.action == iee::IntentAction::Select);
    }

    iee::ExecutionResult Execute(const iee::Intent& intent) override {
        iee::ExecutionResult result;
        result.method = "uia-test";
        result.duration = std::chrono::milliseconds(1);

        if (intent.action == iee::IntentAction::SetValue && intent.params.Get("value").empty()) {
            result.status = iee::ExecutionStatus::FAILED;
            result.verified = false;
            result.message = "value required";
            return result;
        }

        if (intent.params.Has("reveal_action")) {
            observer_.RevealHidden();
        }

        result.status = iee::ExecutionStatus::SUCCESS;
        result.verified = true;
        result.message = "ok";
        return result;
    }

    iee::AdapterScore GetScore() const override {
        iee::AdapterScore score;
        score.reliability = 0.98F;
        score.latency = 8.0F;
        score.confidence = 0.98F;
        return score;
    }

private:
    ActionTestObserver& observer_;
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
        ActionTestObserver observer;

        iee::AdapterRegistry adapters;
        adapters.RegisterAdapter(std::make_shared<ActionTestUiAdapter>(observer));

        iee::CapabilityGraphBuilder graphBuilder;
        iee::IntentResolver resolver;
        iee::Telemetry telemetry;
        iee::IntentRegistry registry(observer, adapters, graphBuilder, resolver, eventBus, telemetry);
        iee::IntentValidator validator;
        iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

        iee::IntentApiServer api(registry, engine, telemetry);

        {
            const std::string body =
                "{\"action\":\"activate\",\"target\":\"Command Palette\",\"context\":{\"app\":\"code\",\"domain\":\"generic\"}}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/act", body));
            AssertTrue(HasStatus(response, 200), "POST /act should execute a simple visible target");
            AssertTrue(response.find("\"status\":\"success\"") != std::string::npos, "Simple /act should succeed");
            AssertTrue(response.find("\"trace_id\":\"") != std::string::npos, "Simple /act should include trace id");
            AssertTrue(response.find("\"resolved_node_id\":\"") != std::string::npos, "Simple /act should include resolved node id");
        }

        {
            const std::string body =
                "{\"action\":\"activate\",\"target\":\"Common Target\",\"context\":{\"app\":\"code\"}}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/act", body));
            AssertTrue(HasStatus(response, 409), "POST /act should return 409 for ambiguous target");
            AssertTrue(response.find("\"reason\":\"ambiguous_target\"") != std::string::npos, "Ambiguous /act should include ambiguity reason");
            AssertTrue(response.find("\"candidates\"") != std::string::npos, "Ambiguous /act should include candidate alternatives");
            AssertTrue(response.find("\"trace_id\":\"") != std::string::npos, "Ambiguous /act should still include trace id");
        }

        {
            const std::string body =
                "{\"action\":\"set_value\",\"target\":\"Search Bar\",\"value\":\"hello world\",\"context\":{\"domain\":\"browser\"}}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/act", body));
            AssertTrue(HasStatus(response, 200), "POST /act should execute set_value flow");
            AssertTrue(response.find("\"status\":\"success\"") != std::string::npos, "set_value /act should succeed");
            AssertTrue(response.find("\"verified\":true") != std::string::npos, "set_value /act should be verified");
        }

        {
            const std::string body =
                "{\"action\":\"activate\",\"target\":\"Export Slide Deck\",\"context\":{\"domain\":\"presentation\"}}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/act", body));
            AssertTrue(HasStatus(response, 200), "POST /act should execute hidden target via reveal path");
            AssertTrue(response.find("\"status\":\"success\"") != std::string::npos, "Hidden /act should succeed");
            AssertTrue(response.find("\"reveal_used\":true") != std::string::npos, "Hidden /act should report reveal usage");
        }

        {
            const std::string body =
                "{\"action\":\"set_value\",\"target\":\"Search Bar\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/act", body));
            AssertTrue(HasStatus(response, 400), "POST /act should reject set_value requests without value");
            AssertTrue(response.find("\"reason\":\"missing_value\"") != std::string::npos, "Missing value should be reported");
            AssertTrue(response.find("\"trace_id\":\"") != std::string::npos, "Missing value failure should still include trace id");
        }

        std::cout << "integration_action_interface: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "integration_action_interface: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
