#include <chrono>
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

class ReflexObserver final : public iee::IObserverEngine {
public:
    iee::ObserverSnapshot Capture() override {
        iee::ObserverSnapshot snapshot;
        snapshot.valid = true;
        snapshot.sequence = ++sequence_;
        snapshot.activeWindowTitle = L"Reflex Test Workspace";
        snapshot.activeProcessPath = L"reflex_test.exe";
        snapshot.cursorPosition = {120, 120};

        iee::UiElement root;
        root.id = "root";
        root.name = L"Root";
        root.controlType = iee::UiControlType::Window;
        root.isEnabled = true;
        root.isVisible = true;
        root.isOffscreen = false;
        root.bounds = {0, 0, 1280, 720};

        iee::UiElement save;
        save.id = "save-button";
        save.parentId = root.id;
        save.name = L"Save";
        save.controlType = iee::UiControlType::Button;
        save.isEnabled = true;
        save.isVisible = true;
        save.isOffscreen = false;
        save.bounds = {32, 32, 192, 88};

        iee::UiElement toolbar;
        toolbar.id = "toolbar";
        toolbar.parentId = root.id;
        toolbar.name = L"Toolbar";
        toolbar.controlType = iee::UiControlType::Document;
        toolbar.isEnabled = true;
        toolbar.isVisible = true;
        toolbar.isOffscreen = false;
        toolbar.bounds = {0, 0, 1280, 120};

        snapshot.uiElements = {root, save, toolbar};
        return snapshot;
    }

private:
    std::uint64_t sequence_{0};
};

class ReflexUiAdapter final : public iee::Adapter {
public:
    std::string Name() const override {
        return "UIAAdapter";
    }

    std::vector<iee::Intent> GetCapabilities(const iee::ObserverSnapshot& snapshot, const iee::CapabilityGraph&) override {
        iee::Intent activate;
        activate.id = "uia-save";
        activate.name = "activate";
        activate.action = iee::IntentAction::Activate;
        activate.source = "uia";
        activate.confidence = 1.0F;
        activate.target.type = iee::TargetType::UiElement;
        activate.target.nodeId = "save-button";
        activate.target.label = L"Save";
        activate.target.screenCenter = snapshot.cursorPosition;

        return {activate};
    }

    bool CanExecute(const iee::Intent& intent) const override {
        return intent.target.type == iee::TargetType::UiElement &&
            (intent.action == iee::IntentAction::Activate || intent.action == iee::IntentAction::Select);
    }

    iee::ExecutionResult Execute(const iee::Intent&) override {
        iee::ExecutionResult result;
        result.status = iee::ExecutionStatus::SUCCESS;
        result.verified = true;
        result.method = "uia-reflex";
        result.message = "ok";
        result.duration = std::chrono::milliseconds(1);
        return result;
    }

    iee::AdapterScore GetScore() const override {
        iee::AdapterScore score;
        score.reliability = 0.97F;
        score.latency = 5.0F;
        score.confidence = 0.97F;
        return score;
    }
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
        iee::AdapterRegistry adapters;
        adapters.RegisterAdapter(std::make_shared<ReflexUiAdapter>());

        ReflexObserver observer;
        iee::CapabilityGraphBuilder graphBuilder;
        iee::IntentResolver resolver;
        iee::Telemetry telemetry;

        iee::IntentRegistry registry(observer, adapters, graphBuilder, resolver, eventBus, telemetry);
        iee::IntentValidator validator;
        iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

        iee::IntentApiServer api(registry, engine, telemetry);

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/ure/world-model"));
            AssertTrue(HasStatus(response, 200), "GET /ure/world-model should return 200");
            AssertTrue(response.find("\"world_model\"") != std::string::npos, "URE world-model response should include world_model");
            AssertTrue(response.find("\"objects\"") != std::string::npos, "URE world-model response should include objects");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/ure/affordances"));
            AssertTrue(HasStatus(response, 200), "GET /ure/affordances should return 200");
            AssertTrue(response.find("\"affordances\"") != std::string::npos, "URE affordances response should include affordances");
            AssertTrue(response.find("\"actions\"") != std::string::npos, "URE affordances response should include actions");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/ure/decision"));
            AssertTrue(HasStatus(response, 200), "GET /ure/decision should return 200");
            AssertTrue(response.find("\"decision\"") != std::string::npos, "URE decision response should include decision");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/ure/step", "{\"execute\":\"true\"}"));
            AssertTrue(HasStatus(response, 200), "POST /ure/step should return 200");
            AssertTrue(response.find("\"step\"") != std::string::npos, "URE step response should include step payload");
            AssertTrue(response.find("\"execution_reason\"") != std::string::npos, "URE step response should include execution reason");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/ure/metrics"));
            AssertTrue(HasStatus(response, 200), "GET /ure/metrics should return 200");
            AssertTrue(response.find("\"decisions\"") != std::string::npos, "URE metrics should include decision count");
            AssertTrue(response.find("\"p95_decision_ms\"") != std::string::npos, "URE metrics should include p95 decision latency");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/ure/experience"));
            AssertTrue(HasStatus(response, 200), "GET /ure/experience should return 200");
            AssertTrue(response.find("[") != std::string::npos, "URE experience should return a JSON array");
        }

        {
            const std::string response = api.HandleRequestForTesting(
                BuildHttpRequest("POST", "/ure/demo", "{\"scenario\":\"unknown_ui\",\"execute\":\"false\"}"));
            AssertTrue(HasStatus(response, 200), "POST /ure/demo should return 200");
            AssertTrue(response.find("\"scenario\":\"unknown_ui\"") != std::string::npos, "URE demo should echo scenario");
            AssertTrue(response.find("\"world_model\"") != std::string::npos, "URE demo should include world model in step payload");
        }

        {
            const std::string policyDown = api.HandleRequestForTesting(
                BuildHttpRequest("POST", "/policy", "{\"allow_execute\":\"false\"}"));
            AssertTrue(HasStatus(policyDown, 200), "POST /policy should allow execution gating for reflex test");

            const std::string blocked = api.HandleRequestForTesting(
                BuildHttpRequest("POST", "/ure/step", "{\"execute\":\"true\"}"));
            AssertTrue(HasStatus(blocked, 200), "POST /ure/step with policy block should still return 200");
            AssertTrue(blocked.find("\"execution_reason\":\"policy_denied\"") != std::string::npos, "URE step should report policy_denied when execution is blocked");

            const std::string policyUp = api.HandleRequestForTesting(
                BuildHttpRequest("POST", "/policy", "{\"allow_execute\":\"true\"}"));
            AssertTrue(HasStatus(policyUp, 200), "POST /policy should restore execute permission");
        }

        std::cout << "integration_universal_reflex: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "integration_universal_reflex: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
