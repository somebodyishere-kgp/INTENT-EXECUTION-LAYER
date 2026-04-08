#include <filesystem>
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

class FakeObserver final : public iee::IObserverEngine {
public:
    iee::ObserverSnapshot Capture() override {
        iee::ObserverSnapshot snapshot;
        snapshot.valid = true;
        snapshot.sequence = ++sequence_;
        snapshot.activeWindowTitle = L"ApiTest";
        snapshot.activeProcessPath = L"api_test.exe";
        snapshot.cursorPosition = {100, 100};

        iee::UiElement save;
        save.id = "ui-save";
        save.name = L"Save";
        save.controlType = iee::UiControlType::Button;
        save.isEnabled = true;
        save.isVisible = true;
        save.isOffscreen = false;
        save.bounds = {40, 20, 200, 80};

        iee::UiElement hiddenMenu;
        hiddenMenu.id = "ui-hidden-export";
        hiddenMenu.name = L"Export";
        hiddenMenu.controlType = iee::UiControlType::MenuItem;
        hiddenMenu.isEnabled = true;
        hiddenMenu.isVisible = false;
        hiddenMenu.isOffscreen = true;
        hiddenMenu.isHidden = true;
        hiddenMenu.acceleratorKey = L"Ctrl+E";
        hiddenMenu.bounds = {10, 200, 220, 260};

        snapshot.uiElements.push_back(std::move(save));
        snapshot.uiElements.push_back(std::move(hiddenMenu));
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

std::string ReadJsonStringField(const std::string& payload, const std::string& field) {
    const std::string marker = "\"" + field + "\":\"";
    const std::size_t start = payload.find(marker);
    if (start == std::string::npos) {
        return "";
    }

    const std::size_t valueStart = start + marker.size();
    const std::size_t valueEnd = payload.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        return "";
    }

    return payload.substr(valueStart, valueEnd - valueStart);
}

std::uint64_t ReadJsonUintField(const std::string& payload, const std::string& field) {
    const std::string marker = "\"" + field + "\":";
    const std::size_t start = payload.find(marker);
    if (start == std::string::npos) {
        return 0;
    }

    const std::size_t valueStart = start + marker.size();
    std::size_t valueEnd = valueStart;
    while (valueEnd < payload.size() && payload[valueEnd] >= '0' && payload[valueEnd] <= '9') {
        ++valueEnd;
    }

    if (valueEnd == valueStart) {
        return 0;
    }

    return static_cast<std::uint64_t>(std::stoull(payload.substr(valueStart, valueEnd - valueStart)));
}

std::string FindNodeIdByUiElementId(const std::string& payload, const std::string& uiElementId) {
    const std::string marker = "\"ui_element_id\":\"" + uiElementId + "\"";
    const std::size_t elementPos = payload.find(marker);
    if (elementPos == std::string::npos) {
        return "";
    }

    const std::string idMarker = "\"id\":\"";
    const std::size_t idPos = payload.rfind(idMarker, elementPos);
    if (idPos == std::string::npos) {
        return "";
    }

    const std::size_t valueStart = idPos + idMarker.size();
    const std::size_t valueEnd = payload.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        return "";
    }

    return payload.substr(valueStart, valueEnd - valueStart);
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
        std::string saveNodeId;
        std::string lastTraceId;
        std::uint64_t initialGraphVersion = 0;

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/health"));
            AssertTrue(HasStatus(response, 200), "GET /health should return 200");
            AssertTrue(response.find("\"status\":\"ok\"") != std::string::npos, "Health response should include status");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/telemetry/persistence"));
            AssertTrue(HasStatus(response, 200), "GET /telemetry/persistence should return 200");
            AssertTrue(response.find("\"enabled\"") != std::string::npos, "Persistence response should include enabled field");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/stream/state"));
            AssertTrue(HasStatus(response, 200), "GET /stream/state should return 200");
            AssertTrue(response.find("\"state\"") != std::string::npos, "Stream state response should include state object");
            AssertTrue(response.find("\"perception\"") != std::string::npos, "Stream state response should include perception");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/state/ai"));
            AssertTrue(HasStatus(response, 200), "GET /state/ai should return 200");
            AssertTrue(response.find("\"interaction_summary\"") != std::string::npos, "AI state response should include interaction summary");
            AssertTrue(response.find("\"dominant_actions\"") != std::string::npos, "AI state response should include dominant actions");
            AssertTrue(response.find("\"filter\"") != std::string::npos, "AI state response should include filter metadata");
        }

        {
            const std::string response = api.HandleRequestForTesting(
                BuildHttpRequest("GET", "/state/ai?filter=relevant&goal=export%20hidden%20menu&domain=presentation&top_n=3"));
            AssertTrue(HasStatus(response, 200), "GET /state/ai relevant filter should return 200");
            AssertTrue(response.find("\"mode\":\"relevant\"") != std::string::npos, "Relevant AI state response should include relevant mode");
            AssertTrue(response.find("\"nodes\"") != std::string::npos, "Relevant AI state response should include filtered node list");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/stream/frame"));
            AssertTrue(HasStatus(response, 200), "GET /stream/frame should return 200");
            AssertTrue(response.find("\"mode\":\"full\"") != std::string::npos, "Stream frame response should default to full mode");
            AssertTrue(response.find("\"state\"") != std::string::npos, "Stream frame response should include full screen state");
            AssertTrue(
                response.find("\"estimated_fps\":") != std::string::npos,
                "Stream frame response should include vision estimated_fps");
            AssertTrue(
                response.find(",,\"capture\"") == std::string::npos,
                "Stream frame response should not contain duplicate comma before capture stats");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/stream/frame?mode=delta"));
            AssertTrue(HasStatus(response, 200), "GET /stream/frame?mode=delta should return 200");
            AssertTrue(response.find("\"mode\":\"delta\"") != std::string::npos, "Delta stream frame response should identify delta mode");
            AssertTrue(response.find("\"delta\"") != std::string::npos, "Delta stream frame response should include delta payload");
            AssertTrue(
                response.find(",,\"capture\"") == std::string::npos,
                "Delta stream frame response should not contain duplicate comma before capture stats");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/stream/live?events=3&interval_ms=50"));
            AssertTrue(HasStatus(response, 200), "GET /stream/live should return 200 in test mode");
            AssertTrue(response.find("\"transport\":\"sse\"") != std::string::npos, "Live stream response should identify SSE transport");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/perf?target_ms=10&limit=64"));
            AssertTrue(HasStatus(response, 200), "GET /perf should return 200");
            AssertTrue(response.find("\"contract\"") != std::string::npos, "Perf response should include contract object");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/perf?target_ms=10&limit=64&strict=true"));
            AssertTrue(HasStatus(response, 200) || HasStatus(response, 409), "GET /perf strict mode should return 200 or 409");
            AssertTrue(response.find("\"strict\":true") != std::string::npos, "Strict perf response should include strict flag");
            AssertTrue(response.find("\"strict_passed\"") != std::string::npos, "Strict perf response should include strict pass field");
            AssertTrue(
                response.find("\"sample_activation_seeded\"") != std::string::npos,
                "Strict perf response should include sample activation metadata");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/capabilities"));
            AssertTrue(HasStatus(response, 200), "GET /capabilities should return 200");
            AssertTrue(response.find("\"capabilities\"") != std::string::npos, "Capabilities response should include capabilities array");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/capabilities/full"));
            AssertTrue(HasStatus(response, 200), "GET /capabilities/full should return 200");
            AssertTrue(response.find("\"capabilities\"") != std::string::npos, "Full capabilities should include capabilities array");
            AssertTrue(response.find("\"hidden_node_count\"") != std::string::npos, "Full capabilities should include hidden node count");
            AssertTrue(response.find("\"graph_version\"") != std::string::npos, "Full capabilities should include graph version");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/interaction-graph"));
            AssertTrue(HasStatus(response, 200), "GET /interaction-graph should return 200");
            AssertTrue(response.find("\"graph\"") != std::string::npos, "Interaction graph response should include graph payload");
            AssertTrue(response.find("\"version\"") != std::string::npos, "Interaction graph response should include graph version");
            saveNodeId = FindNodeIdByUiElementId(response, "ui-save");
            AssertTrue(!saveNodeId.empty(), "Interaction graph should expose stable node ids for ui-save");
            initialGraphVersion = ReadJsonUintField(response, "version");
            AssertTrue(initialGraphVersion > 0, "Interaction graph version should be non-zero");
        }

        {
            const std::string response = api.HandleRequestForTesting(
                BuildHttpRequest("GET", "/interaction-node/" + saveNodeId));
            AssertTrue(HasStatus(response, 200), "GET /interaction-node/{id} should return 200");
            AssertTrue(response.find("\"node\"") != std::string::npos, "Interaction node response should include node payload");
            AssertTrue(response.find("\"intent\"") != std::string::npos, "Interaction node response should include deterministic intent payload");
            AssertTrue(response.find("\"execution_plan\"") != std::string::npos, "Interaction node response should include execution plan");
            AssertTrue(response.find("\"reveal_strategy\"") != std::string::npos, "Interaction node response should include reveal strategy");
            AssertTrue(response.find("\"intent_binding\"") != std::string::npos, "Interaction node response should include intent binding");
        }

        {
            const std::string response = api.HandleRequestForTesting(
                BuildHttpRequest("GET", "/interaction-graph?delta_since=" + std::to_string(initialGraphVersion)));
            AssertTrue(HasStatus(response, 200), "GET /interaction-graph?delta_since should return 200");
            AssertTrue(response.find("\"delta\"") != std::string::npos, "Delta graph response should include delta payload");
            AssertTrue(response.find("\"changed\":false") != std::string::npos, "Equivalent frames should produce unchanged graph delta");
        }

        {
            const std::string response = api.HandleRequestForTesting(
                BuildHttpRequest("GET", "/interaction-graph?delta_since=9999999"));
            AssertTrue(HasStatus(response, 200), "GET /interaction-graph with stale delta_since should return 200");
            AssertTrue(response.find("\"reset_required\":true") != std::string::npos, "Stale delta_since should request graph reset");
        }

        {
            const std::string body = "{\"action\":\"activate\",\"target\":\"Save\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/explain", body));
            AssertTrue(HasStatus(response, 200), "POST /explain should return 200");
            AssertTrue(response.find("\"candidates\"") != std::string::npos, "Explain response should include candidates");
        }

        {
            const std::string body = "{\"goal\":\"export hidden menu\",\"target\":\"Export\",\"domain\":\"presentation\",\"allow_hidden\":\"true\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/task/plan", body));
            AssertTrue(HasStatus(response, 200), "POST /task/plan should return 200");
            AssertTrue(response.find("\"planning_only\":true") != std::string::npos, "Task plan response should be planning-only");
            AssertTrue(response.find("\"task_plan\"") != std::string::npos, "Task plan response should include task plan payload");
            AssertTrue(response.find("\"plans\"") != std::string::npos, "Task plan response should include ranked plans payload");
            AssertTrue(response.find("\"plan_score\"") != std::string::npos, "Task plan response should include plan score metadata");
        }

        {
            const std::filesystem::path tempPath = "api_hardening_create.txt";
            const std::string body = "{\"action\":\"create\",\"path\":\"api_hardening_create.txt\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/execute", body));
            AssertTrue(HasStatus(response, 200), "POST /execute should return 200 for create");
            AssertTrue(response.find("\"trace_id\"") != std::string::npos, "Execution response should include trace id");
            lastTraceId = ReadJsonStringField(response, "trace_id");
            AssertTrue(std::filesystem::exists(tempPath), "Create request should materialize file");
            if (std::filesystem::exists(tempPath)) {
                std::filesystem::remove(tempPath);
            }
        }

        {
            AssertTrue(!lastTraceId.empty(), "Execution trace id should be populated before trace fetch");
            const std::string response = api.HandleRequestForTesting(
                BuildHttpRequest("GET", "/trace/" + lastTraceId));
            AssertTrue(HasStatus(response, 200), "GET /trace/{trace_id} should return 200");
            AssertTrue(response.find("\"trace_id\"") != std::string::npos, "Trace lookup should include trace payload");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("GET", "/trace/does-not-exist"));
            AssertTrue(HasStatus(response, 404), "GET /trace/{trace_id} should return 404 for unknown traces");
        }

        {
            const std::filesystem::path tempPath = "api_stream_control_create.txt";
            const std::string body = "{\"action\":\"create\",\"path\":\"api_stream_control_create.txt\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/stream/control", body));
            AssertTrue(HasStatus(response, 200), "POST /stream/control should return 200 for immediate single-step execution");
            AssertTrue(response.find("\"success\":true") != std::string::npos, "Stream control response should report success");
            AssertTrue(std::filesystem::exists(tempPath), "Stream control should materialize file");
            if (std::filesystem::exists(tempPath)) {
                std::filesystem::remove(tempPath);
            }
        }

        {
            const std::string body =
                "{\"sequence\":\"create|api_stream_macro.txt;move|api_stream_macro.txt|api_stream_macro_moved.txt;delete|api_stream_macro_moved.txt\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/stream/control", body));
            AssertTrue(HasStatus(response, 200), "POST /stream/control should support macro sequence execution");
            AssertTrue(response.find("\"attempted_steps\":3") != std::string::npos, "Macro sequence should execute three steps");
            AssertTrue(!std::filesystem::exists("api_stream_macro.txt"), "Macro sequence should move/delete source file");
            AssertTrue(!std::filesystem::exists("api_stream_macro_moved.txt"), "Macro sequence should delete moved file");
        }

        {
            const std::filesystem::path loopPath = "api_stream_macro_loop.txt";
            const std::string body =
                "{\"sequence\":\"loop|2|create:api_stream_macro_loop.txt;if_visible|Save|delete:api_stream_macro_loop.txt|delete:api_stream_macro_loop.txt\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/stream/control", body));
            AssertTrue(HasStatus(response, 200), "Macro v2 loop/conditional sequence should return 200");
            AssertTrue(response.find("\"attempted_steps\":3") != std::string::npos, "Macro v2 sequence should execute loop + branch steps");
            AssertTrue(!std::filesystem::exists(loopPath), "Macro v2 else branch should delete generated file when target is not visible");
        }

        {
            const std::string body = "{\"action\":\"create\",\"path\":\"api_predict_preview.txt\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/predict", body));
            AssertTrue(HasStatus(response, 200), "POST /predict should return 200");
            AssertTrue(response.find("\"before\"") != std::string::npos, "Predict response should include before state");
            AssertTrue(response.find("\"after\"") != std::string::npos, "Predict response should include after state");
            AssertTrue(response.find("\"delta\"") != std::string::npos, "Predict response should include delta object");
            AssertTrue(!std::filesystem::exists("api_predict_preview.txt"), "Predict should not execute actions");
        }

        {
            const std::string response = api.HandleRequestForTesting(
                BuildHttpRequest("POST", "/control/start", "{\"latencyBudgetMs\":\"2\"}"));
            AssertTrue(HasStatus(response, 200), "POST /control/start should return 200");
            AssertTrue(response.find("\"started\":true") != std::string::npos, "Control start should report started=true");
        }

        {
            const std::string status = api.HandleRequestForTesting(BuildHttpRequest("GET", "/control/status"));
            AssertTrue(HasStatus(status, 200), "GET /control/status should return 200");
            AssertTrue(status.find("\"active\":true") != std::string::npos, "Control status should report active runtime");
        }

        {
            const std::string body = "{\"action\":\"activate\",\"target\":\"Save\",\"mode\":\"queued\",\"priority\":\"high\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/execute", body));
            AssertTrue(HasStatus(response, 202), "Queued execute should return 202");
            AssertTrue(response.find("\"queued\":true") != std::string::npos, "Queued execute should report queued=true");
        }

        {
            const std::string body =
                "{\"action\":\"activate\",\"target\":\"Save\",\"mode\":\"queued\",\"priority\":\"high\",\"repeat\":\"2\"}";
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/stream/control", body));
            AssertTrue(HasStatus(response, 202), "Queued stream control should return 202");
            AssertTrue(response.find("\"queued_count\":2") != std::string::npos, "Queued stream control should enqueue two steps");
        }

        {
            const std::string response = api.HandleRequestForTesting(BuildHttpRequest("POST", "/control/stop", "{}"));
            AssertTrue(HasStatus(response, 200), "POST /control/stop should return 200");
            AssertTrue(response.find("\"summary\"") != std::string::npos, "Control stop response should include summary");
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
