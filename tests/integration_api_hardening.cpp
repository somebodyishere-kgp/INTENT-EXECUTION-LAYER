#include <filesystem>
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

class FakeObserver final : public iee::IObserverEngine {
public:
    iee::ObserverSnapshot Capture() override {
        iee::ObserverSnapshot snapshot;
        snapshot.valid = true;
        snapshot.sequence = ++sequence_;
        snapshot.activeWindowTitle = L"ApiTest";
        snapshot.activeProcessPath = L"api_test.exe";
        snapshot.cursorPosition = {100, 100};
        return snapshot;
    }

private:
    std::uint64_t sequence_{0};
};

class FakeUiAdapter final : public iee::Adapter {
public:
    std::string Name() const override {
        return "UIAAdapter";
    }

    std::vector<iee::Intent> GetCapabilities(const iee::ObserverSnapshot& snapshot, const iee::CapabilityGraph&) override {
        iee::Intent intent;
        intent.id = "uia-save";
        intent.name = "activate";
        intent.action = iee::IntentAction::Activate;
        intent.source = "uia";
        intent.confidence = 1.0F;
        intent.target.type = iee::TargetType::UiElement;
        intent.target.label = L"Save";
        intent.target.focused = true;
        intent.target.hierarchyDepth = 1;
        intent.target.screenCenter = snapshot.cursorPosition;
        intent.context.cursor = snapshot.cursorPosition;
        return {intent};
    }

    bool CanExecute(const iee::Intent&) const override {
        return false;
    }

    iee::ExecutionResult Execute(const iee::Intent&) override {
        return {};
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
        adapters.RegisterAdapter(std::make_shared<FakeUiAdapter>());
        adapters.Register(std::make_unique<iee::FileSystemAdapter>());

        FakeObserver observer;
        iee::CapabilityGraphBuilder graphBuilder;
        iee::IntentResolver resolver;
        iee::Telemetry telemetry;

        iee::IntentRegistry registry(observer, adapters, graphBuilder, resolver, eventBus, telemetry);
        iee::IntentValidator validator;
        iee::ExecutionEngine engine(adapters, eventBus, validator, telemetry);

        iee::IntentApiServer api(registry, engine, telemetry);

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/health"));
            AssertTrue(HasStatus(response, 200), "GET /health should return 200");
            AssertTrue(response.find("\"status\":\"ok\"") != std::string::npos, "Health response should include status");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/capabilities"));
            AssertTrue(HasStatus(response, 200), "GET /capabilities should return 200");
            AssertTrue(response.find("\"capabilities\"") != std::string::npos, "Capabilities response should include capabilities array");
        }

        {
            const std::string body = "{\"action\":\"activate\",\"target\":\"Save\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/explain", body));
            AssertTrue(HasStatus(response, 200), "POST /explain should return 200");
            AssertTrue(response.find("\"candidates\"") != std::string::npos, "Explain response should include candidates");
        }

        {
            const std::filesystem::path tempPath = "api_hardening_create.txt";
            const std::string body = "{\"action\":\"create\",\"path\":\"api_hardening_create.txt\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/execute", body));
            AssertTrue(HasStatus(response, 200), "POST /execute should return 200 for create");
            AssertTrue(response.find("\"trace_id\"") != std::string::npos, "Execution response should include trace id");
            AssertTrue(std::filesystem::exists(tempPath), "Create request should materialize file");
            if (std::filesystem::exists(tempPath)) {
                std::filesystem::remove(tempPath);
            }
        }

        {
            const std::string invalidJsonResponse =
                api.HandleRequestForTesting(BuildHttpRequest("POST", "/execute", "{\"action\":\"create\","));
            AssertTrue(HasStatus(invalidJsonResponse, 400), "Invalid JSON should return 400");
            AssertTrue(invalidJsonResponse.find("\"error\"") != std::string::npos, "Invalid JSON response should be structured");
        }

        {
            const std::string missingAction =
                api.HandleRequestForTesting(BuildHttpRequest("POST", "/execute", "{\"path\":\"x.txt\"}"));
            AssertTrue(HasStatus(missingAction, 400), "Missing action should return 400");
            AssertTrue(missingAction.find("missing_action") != std::string::npos, "Missing action code should be returned");
        }

        std::cout << "integration_api_hardening: PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "integration_api_hardening: FAIL - " << ex.what() << "\n";
        return 1;
    }
}
