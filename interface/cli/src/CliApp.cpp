#include "CliApp.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <system_error>

#include "ActionInterface.h"
#include "ExecutionContract.h"
#include "AIStateView.h"
#include "EnvironmentAdapter.h"
#include "InteractionGraph.h"
#include "Intent.h"
#include "IntentApiServer.h"
#include "Logger.h"
#include "TaskInterface.h"

namespace iee {
namespace {

std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int requiredChars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (requiredChars <= 0) {
        return L"";
    }

    std::wstring result(static_cast<std::size_t>(requiredChars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), requiredChars);
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

std::string ToNarrow(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (requiredBytes <= 0) {
        return "";
    }

    std::string result(static_cast<std::size_t>(requiredBytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), requiredBytes, nullptr, nullptr);
    if (!result.empty() && result.back() == '\0') {
        result.pop_back();
    }
    return result;
}

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 16U);

    for (const char ch : value) {
        switch (ch) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

std::string ReadOption(const ParsedCommand& command, const std::string& key) {
    const auto it = command.options.find(key);
    if (it == command.options.end()) {
        return "";
    }
    return it->second;
}

bool HasOption(const ParsedCommand& command, const std::string& key) {
    return command.options.find(key) != command.options.end();
}

bool WantsJson(const ParsedCommand& command) {
    return HasOption(command, "json") || HasOption(command, "pure-json");
}

std::size_t ReadSizeOption(
    const ParsedCommand& command,
    const std::string& key,
    std::size_t defaultValue,
    std::size_t maxValue) {
    const std::string value = ReadOption(command, key);
    if (value.empty()) {
        return defaultValue;
    }

    unsigned long long parsed = 0;
    const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc() || ptr != value.data() + value.size()) {
        return defaultValue;
    }

    const std::size_t bounded = static_cast<std::size_t>(std::min<unsigned long long>(parsed, maxValue));
    if (bounded == 0U) {
        return defaultValue;
    }

    return bounded;
}

double ReadDoubleOption(
    const ParsedCommand& command,
    const std::string& key,
    double defaultValue,
    double minValue,
    double maxValue) {
    const std::string value = ReadOption(command, key);
    if (value.empty()) {
        return defaultValue;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || end == nullptr || *end != '\0') {
        return defaultValue;
    }

    return std::clamp(parsed, minValue, maxValue);
}

std::uint16_t ReadPort(const ParsedCommand& command, std::uint16_t defaultValue) {
    const std::string value = ReadOption(command, "port");
    if (value.empty()) {
        return defaultValue;
    }

    unsigned int parsed = defaultValue;
    const auto [ptr, parseError] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    constexpr unsigned int kMaxPort = 65535U;
    if (parseError != std::errc() || ptr != value.data() + value.size() || parsed > kMaxPort) {
        return defaultValue;
    }

    return static_cast<std::uint16_t>(parsed);
}

bool IsFileIntent(IntentAction action) {
    return action == IntentAction::Create || action == IntentAction::Delete || action == IntentAction::Move;
}

std::wstring PrimaryTargetText(const Intent& intent) {
    if (!intent.target.label.empty()) {
        return intent.target.label;
    }
    if (!intent.target.automationId.empty()) {
        return intent.target.automationId;
    }
    return intent.target.path;
}

bool IsSuccess(ExecutionStatus status) {
    return status == ExecutionStatus::SUCCESS || status == ExecutionStatus::PARTIAL;
}

struct ExplainInput {
    IntentAction action{IntentAction::Unknown};
    std::wstring target;
};

ExplainInput ParseExplainInput(const ParsedCommand& command) {
    ExplainInput input;

    std::string action = ReadOption(command, "action");
    std::string target = ReadOption(command, "target");

    if (action.empty() && !command.positionals.empty()) {
        const std::string first = command.positionals[0];
        const std::size_t firstSpace = first.find(' ');
        if (firstSpace != std::string::npos) {
            action = first.substr(0, firstSpace);
            target = first.substr(firstSpace + 1U);
        } else {
            action = first;
            if (command.positionals.size() > 1U) {
                target = command.positionals[1];
            }
        }
    }

    input.action = IntentActionFromString(action);
    input.target = ToWide(target);
    return input;
}

std::string JoinPositionals(const ParsedCommand& command) {
    std::ostringstream joined;
    for (std::size_t index = 0; index < command.positionals.size(); ++index) {
        if (index > 0) {
            joined << " ";
        }
        joined << command.positionals[index];
    }
    return joined.str();
}

std::string ToAsciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ParseActPhrase(const std::string& phrase, ActionRequest* request) {
    if (request == nullptr) {
        return false;
    }

    const std::string normalized = ToAsciiLower(phrase);
    if (normalized.empty()) {
        return false;
    }

    ActionRequest parsed;

    const auto startsWith = [&normalized](const char* prefix) {
        const std::size_t length = std::char_traits<char>::length(prefix);
        return normalized.size() >= length && normalized.compare(0, length, prefix) == 0;
    };

    if (startsWith("click ") || startsWith("open ") || startsWith("activate ")) {
        parsed.action = "activate";
        const std::size_t separator = normalized.find(' ');
        parsed.target = separator == std::string::npos ? "" : phrase.substr(separator + 1U);
    } else if (startsWith("select ")) {
        parsed.action = "select";
        parsed.target = phrase.substr(std::string("select ").size());
    } else if (startsWith("type ")) {
        parsed.action = "set_value";
        const std::size_t inPos = normalized.find(" in ");
        if (inPos == std::string::npos) {
            parsed.value = phrase.substr(std::string("type ").size());
            parsed.target = "focused input";
        } else {
            parsed.value = phrase.substr(std::string("type ").size(), inPos - std::string("type ").size());
            parsed.target = phrase.substr(inPos + std::string(" in ").size());
        }
    } else if (startsWith("navigate ")) {
        parsed.action = "navigate";
        parsed.target = "address bar";
        parsed.value = phrase.substr(std::string("navigate ").size());
    } else if (startsWith("go to ")) {
        parsed.action = "navigate";
        parsed.target = "address bar";
        parsed.value = phrase.substr(std::string("go to ").size());
    } else {
        return false;
    }

    if (parsed.target.empty()) {
        return false;
    }

    *request = std::move(parsed);
    return true;
}

InteractionGraph BuildLatestInteractionGraph(IntentRegistry& intentRegistry, ObserverSnapshot* snapshotOut = nullptr) {
    intentRegistry.Refresh();
    const ObserverSnapshot snapshot = intentRegistry.LastSnapshot();
    if (snapshotOut != nullptr) {
        *snapshotOut = snapshot;
    }
    if (!snapshot.valid) {
        return InteractionGraph{};
    }

    return InteractionGraphBuilder::Build(snapshot.uiElements, snapshot.sequence);
}

std::string ResolveNodeIdArgument(const ParsedCommand& command) {
    std::string nodeId = ReadOption(command, "id");
    if (nodeId.empty() && !command.positionals.empty()) {
        nodeId = command.positionals.front();
    }
    return nodeId;
}

std::string BuildHttpRequest(const std::string& method, const std::string& path, const std::string& body = "") {
    std::ostringstream stream;
    stream << method << " " << path << " HTTP/1.1\r\n";
    stream << "Host: 127.0.0.1\r\n";
    stream << "Content-Type: application/json\r\n";
    stream << "Content-Length: " << body.size() << "\r\n\r\n";
    stream << body;
    return stream.str();
}

int ParseHttpStatus(const std::string& response) {
    const std::string marker = "HTTP/1.1 ";
    const std::size_t pos = response.find(marker);
    if (pos == std::string::npos || pos + marker.size() + 3U > response.size()) {
        return 0;
    }

    int status = 0;
    const std::string statusText = response.substr(pos + marker.size(), 3U);
    const auto [ptr, error] = std::from_chars(statusText.data(), statusText.data() + statusText.size(), status);
    if (error != std::errc() || ptr != statusText.data() + statusText.size()) {
        return 0;
    }
    return status;
}

std::string ExtractHttpBody(const std::string& response) {
    const std::size_t bodyPos = response.find("\r\n\r\n");
    return bodyPos == std::string::npos ? std::string() : response.substr(bodyPos + 4U);
}

bool IsTruthyString(const std::string& value, bool defaultValue) {
    if (value.empty()) {
        return defaultValue;
    }

    const std::string normalized = ToAsciiLower(value);
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return defaultValue;
}

}  // namespace

CliApp::CliApp(IntentRegistry& intentRegistry, ExecutionEngine& executionEngine, Telemetry& telemetry)
    : intentRegistry_(intentRegistry), executionEngine_(executionEngine), telemetry_(telemetry) {}

int CliApp::Run(int argc, char* argv[]) {
    const ParsedCommand command = CliParser::Parse(argc, argv);
    const bool pureJson = HasOption(command, "pure-json");
    Logger::SetEnabled(!pureJson);

    if (command.command.empty()) {
        if (pureJson) {
            std::cout << "{\"error\":{\"code\":\"missing_command\",\"message\":\"No command provided\"}}\n";
            return 1;
        }

        CliParser::PrintHelp();
        return 1;
    }

    if (command.command == "state") {
        return HandleState(command, false);
    }

    if (command.command == "state/ai" || command.command == "state-ai") {
        return HandleState(command, true);
    }

    if (command.command == "list-intents") {
        return HandleListIntents(command);
    }

    if (command.command == "execute") {
        return HandleExecute(command);
    }

    if (command.command == "act") {
        return HandleAct(command);
    }

    if (command.command == "inspect") {
        return HandleInspect(command);
    }

    if (command.command == "graph") {
        return HandleGraph(command);
    }

    if (command.command == "node") {
        return HandleNode(command);
    }

    if (command.command == "plan") {
        return HandlePlan(command);
    }

    if (command.command == "reveal") {
        return HandleReveal(command);
    }

    if (command.command == "capabilities") {
        return HandleCapabilities(command);
    }

    if (command.command == "explain") {
        return HandleExplain(command);
    }

    if (command.command == "debug-intents") {
        return HandleDebugIntents(command);
    }

    if (command.command == "api") {
        return HandleApi(command);
    }

    if (command.command == "telemetry") {
        return HandleTelemetry(command);
    }

    if (command.command == "trace") {
        return HandleTrace(command);
    }

    if (command.command == "latency") {
        return HandleLatency(command);
    }

    if (command.command == "perf") {
        return HandlePerf(command);
    }

    if (command.command == "vision") {
        return HandleVision(command);
    }

    if (command.command == "ure") {
        return HandleUre(command);
    }

    if (command.command == "demo") {
        return HandleDemo(command);
    }

    if (pureJson) {
        std::cout << "{\"error\":{\"code\":\"unknown_command\",\"message\":\"Unknown command: "
                  << EscapeJson(command.command) << "\"}}\n";
        return 1;
    }

    CliParser::PrintHelp();
    return 1;
}

int CliApp::HandleState(const ParsedCommand& command, bool aiView) {
    RegistryEnvironmentAdapter adapter(intentRegistry_);
    EnvironmentState state;
    std::string error;
    if (!adapter.CaptureState(&state, &error)) {
        std::cerr << "Unable to capture environment state";
        if (!error.empty()) {
            std::cerr << ": " << error;
        }
        std::cerr << "\n";
        return 1;
    }

    if (aiView) {
        AIStateViewProjector projector;
        const AIStateView view = projector.Build(state, false);

        if (WantsJson(command) || command.command == "state/ai") {
            std::cout << AIStateViewProjector::SerializeJson(view) << "\n";
            return 0;
        }

        std::cout << "AI state view\n";
        std::cout << "  Sequence       : " << view.sequence << "\n";
        std::cout << "  Frame ID       : " << view.frameId << "\n";
        std::cout << "  Graph version  : " << view.graphVersion << "\n";
        std::cout << "  Graph signature: " << view.graphSignature << "\n";
        std::cout << "  Nodes          : " << view.nodeCount << "\n";
        std::cout << "  Hidden nodes   : " << view.hiddenNodeCount << "\n";
        std::cout << "  Actionable     : " << view.actionableNodeCount << "\n";
        return 0;
    }

    std::size_t hiddenCount = 0;
    for (const auto& entry : state.unifiedState.interactionGraph.nodes) {
        const InteractionNode& node = entry.second;
        if (node.hidden || node.offscreen || node.collapsed) {
            ++hiddenCount;
        }
    }

    if (WantsJson(command)) {
        std::ostringstream json;
        json << "{";
        json << "\"sequence\":" << state.sequence << ",";
        json << "\"captured_at_ms\":"
             << std::chrono::duration_cast<std::chrono::milliseconds>(state.capturedAt.time_since_epoch()).count()
             << ",";
        json << "\"active_window_title\":\"" << EscapeJson(ToNarrow(state.activeWindowTitle)) << "\",";
        json << "\"active_process_path\":\"" << EscapeJson(ToNarrow(state.activeProcessPath)) << "\",";
        json << "\"cursor\":{";
        json << "\"x\":" << state.cursorPosition.x << ",";
        json << "\"y\":" << state.cursorPosition.y;
        json << "},";
        json << "\"ui_element_count\":" << state.uiElements.size() << ",";
        json << "\"filesystem_entry_count\":" << state.fileSystemEntries.size() << ",";
        json << "\"graph_version\":" << state.unifiedState.interactionGraph.version << ",";
        json << "\"graph_signature\":" << state.unifiedState.interactionGraph.signature << ",";
        json << "\"node_count\":" << state.unifiedState.interactionGraph.nodes.size() << ",";
        json << "\"hidden_node_count\":" << hiddenCount;
        json << "}";
        std::cout << json.str() << "\n";
        return 0;
    }

    std::cout << "Environment state\n";
    std::cout << "  Sequence       : " << state.sequence << "\n";
    std::cout << "  Window         : " << ToNarrow(state.activeWindowTitle) << "\n";
    std::cout << "  Process        : " << ToNarrow(state.activeProcessPath) << "\n";
    std::cout << "  Cursor         : (" << state.cursorPosition.x << ", " << state.cursorPosition.y << ")\n";
    std::cout << "  UI elements    : " << state.uiElements.size() << "\n";
    std::cout << "  FS entries     : " << state.fileSystemEntries.size() << "\n";
    std::cout << "  Graph version  : " << state.unifiedState.interactionGraph.version << "\n";
    std::cout << "  Graph signature: " << state.unifiedState.interactionGraph.signature << "\n";
    std::cout << "  Nodes          : " << state.unifiedState.interactionGraph.nodes.size() << "\n";
    std::cout << "  Hidden nodes   : " << hiddenCount << "\n";
    return 0;
}

int CliApp::HandleListIntents(const ParsedCommand& command) {
    intentRegistry_.Refresh();
    const auto intents = intentRegistry_.ListIntents();

    if (WantsJson(command)) {
        std::cout << "[";
        for (std::size_t index = 0; index < intents.size(); ++index) {
            if (index > 0) {
                std::cout << ",";
            }
            std::cout << intents[index].Serialize();
        }
        std::cout << "]\n";
        return 0;
    }

    std::cout << std::left << std::setw(16) << "ACTION"
              << std::setw(36) << "TARGET"
              << std::setw(12) << "CONFIDENCE"
              << std::setw(14) << "SOURCE"
              << "INTENT_ID" << "\n";
    std::cout << std::string(122, '-') << "\n";

    for (const auto& intent : intents) {
        std::cout << std::left << std::setw(16) << ToString(intent.action)
                  << std::setw(36) << ToNarrow(PrimaryTargetText(intent))
                  << std::setw(12) << std::fixed << std::setprecision(2) << intent.confidence
                  << std::setw(14) << intent.source
                  << intent.id << "\n";
    }

    std::cout << "\nTotal intents: " << intents.size() << "\n";
    return 0;
}

int CliApp::HandleExecute(const ParsedCommand& command) {
    const bool jsonMode = WantsJson(command);
    const IntentAction action = IntentActionFromString(command.action);
    if (action == IntentAction::Unknown) {
        if (jsonMode) {
            std::cout << "{\"error\":{\"code\":\"unknown_action\",\"message\":\"Unknown intent action: "
                      << EscapeJson(command.action) << "\"}}\n";
        } else {
            std::cerr << "Unknown intent action: " << command.action << "\n";
        }
        return 1;
    }

    intentRegistry_.Refresh();

    Intent intent;
    intent.action = action;
    intent.name = ToString(action);
    intent.confidence = 1.0;
    intent.source = "cli";

    if (!IsFileIntent(action)) {
        const std::string target = ReadOption(command, "target");
        if (target.empty()) {
            if (jsonMode) {
                std::cout << "{\"error\":{\"code\":\"missing_target\",\"message\":\"Missing --target for UI intent\"}}\n";
            } else {
                std::cerr << "Missing --target for UI intent\n";
            }
            return 1;
        }

        const std::wstring wideTarget = ToWide(target);
        const ResolutionResult resolution = intentRegistry_.Resolve(action, wideTarget);
        if (resolution.ambiguity.has_value()) {
            if (jsonMode) {
                std::cout << "{";
                std::cout << "\"error\":{";
                std::cout << "\"code\":\"ambiguous_target\",";
                std::cout << "\"message\":\"Ambiguous target: " << EscapeJson(target) << "\",";
                std::cout << "\"candidates\":[";
                for (std::size_t index = 0; index < resolution.ambiguity->candidates.size(); ++index) {
                    if (index > 0) {
                        std::cout << ",";
                    }
                    std::cout << "{";
                    std::cout << "\"action\":\""
                              << EscapeJson(ToString(resolution.ambiguity->candidates[index].intent.action)) << "\",";
                    std::cout << "\"target\":\""
                              << EscapeJson(ToNarrow(PrimaryTargetText(resolution.ambiguity->candidates[index].intent))) << "\",";
                    std::cout << "\"score\":" << resolution.ambiguity->candidates[index].score;
                    std::cout << "}";
                }
                std::cout << "]";
                std::cout << "}";
                std::cout << "}\n";
            } else {
                std::cerr << "Ambiguous target: " << target << "\n";
                for (const auto& candidate : resolution.ambiguity->candidates) {
                    std::cerr << "  - " << ToString(candidate.intent.action)
                              << " target=\"" << ToNarrow(PrimaryTargetText(candidate.intent))
                              << "\" score=" << std::fixed << std::setprecision(3) << candidate.score
                              << "\n";
                }
            }
            return 2;
        }

        if (resolution.bestMatch.has_value()) {
            intent = resolution.bestMatch->intent;
        }

        intent.action = action;
        intent.name = ToString(action);
        intent.target.type = TargetType::UiElement;
        intent.target.label = wideTarget;

        if (action == IntentAction::SetValue) {
            const std::string value = ReadOption(command, "value");
            if (value.empty()) {
                if (jsonMode) {
                    std::cout << "{\"error\":{\"code\":\"missing_value\",\"message\":\"Missing --value for set_value\"}}\n";
                } else {
                    std::cerr << "Missing --value for set_value\n";
                }
                return 1;
            }
            intent.params.values["value"] = ToWide(value);
        }
    } else {
        intent.target.type = TargetType::FileSystemPath;
        if (action == IntentAction::Create) {
            const std::string path = ReadOption(command, "path");
            if (path.empty()) {
                if (jsonMode) {
                    std::cout << "{\"error\":{\"code\":\"missing_path\",\"message\":\"Missing --path for create\"}}\n";
                } else {
                    std::cerr << "Missing --path for create\n";
                }
                return 1;
            }
            intent.params.values["path"] = ToWide(path);
            intent.target.path = intent.params.values["path"];
            intent.target.label = intent.params.values["path"];
        } else if (action == IntentAction::Delete) {
            const std::string path = ReadOption(command, "path");
            if (path.empty()) {
                if (jsonMode) {
                    std::cout << "{\"error\":{\"code\":\"missing_path\",\"message\":\"Missing --path for delete\"}}\n";
                } else {
                    std::cerr << "Missing --path for delete\n";
                }
                return 1;
            }
            intent.params.values["path"] = ToWide(path);
            intent.target.path = intent.params.values["path"];
            intent.target.label = intent.params.values["path"];
        } else if (action == IntentAction::Move) {
            const std::string path = ReadOption(command, "path");
            const std::string destination = ReadOption(command, "destination");
            if (path.empty() || destination.empty()) {
                if (jsonMode) {
                    std::cout << "{\"error\":{\"code\":\"missing_move_path\",\"message\":\"Missing --path or --destination for move\"}}\n";
                } else {
                    std::cerr << "Missing --path or --destination for move\n";
                }
                return 1;
            }
            intent.params.values["path"] = ToWide(path);
            intent.params.values["destination"] = ToWide(destination);
            intent.target.path = intent.params.values["path"];
            intent.target.label = intent.params.values["path"];
        }
    }

    const ExecutionResult result = executionEngine_.Execute(intent);
    if (jsonMode) {
        std::cout << "{";
        std::cout << "\"trace_id\":\"" << EscapeJson(result.traceId) << "\",";
        std::cout << "\"method\":\"" << EscapeJson(result.method) << "\",";
        std::cout << "\"status\":\"" << EscapeJson(ToString(result.status)) << "\",";
        std::cout << "\"verified\":" << (result.verified ? "true" : "false") << ",";
        std::cout << "\"used_fallback\":" << (result.usedFallback ? "true" : "false") << ",";
        std::cout << "\"attempts\":" << result.attempts << ",";
        std::cout << "\"duration_ms\":" << result.duration.count() << ",";
        std::cout << "\"message\":\"" << EscapeJson(result.message) << "\"";
        std::cout << "}\n";

        if (IsSuccess(result.status) && !intent.id.empty()) {
            intentRegistry_.RecordInteraction(intent.id);
        }

        return IsSuccess(result.status) ? 0 : 1;
    }

    std::cout << "Execution method: " << result.method << "\n";
    std::cout << "Status: " << ToString(result.status) << "\n";
    std::cout << "Verified: " << (result.verified ? "true" : "false") << "\n";
    std::cout << "Attempts: " << result.attempts << "\n";
    std::cout << "Duration(ms): " << result.duration.count() << "\n";
    std::cout << "Message: " << result.message << "\n";

    if (IsSuccess(result.status) && !intent.id.empty()) {
        intentRegistry_.RecordInteraction(intent.id);
    }

    return IsSuccess(result.status) ? 0 : 1;
}

int CliApp::HandleAct(const ParsedCommand& command) {
    const bool jsonMode = WantsJson(command);

    ActionRequest request;
    request.action = ReadOption(command, "action");
    request.target = ReadOption(command, "target");
    request.value = ReadOption(command, "value");
    request.context.app = ReadOption(command, "app");
    request.context.domain = ReadOption(command, "domain");

    if (request.action.empty() || request.target.empty()) {
        const std::string phrase = JoinPositionals(command);
        ActionRequest phraseRequest;
        if (!phrase.empty() && ParseActPhrase(phrase, &phraseRequest)) {
            if (request.action.empty()) {
                request.action = phraseRequest.action;
            }
            if (request.target.empty()) {
                request.target = phraseRequest.target;
            }
            if (request.value.empty()) {
                request.value = phraseRequest.value;
            }
        }
    }

    if (request.action.empty() || request.target.empty()) {
        if (jsonMode) {
            ActionExecutionResult invalid;
            invalid.status = "failure";
            invalid.traceId = telemetry_.NewTraceId();
            invalid.reason = "invalid_act_request";
            std::cout << SerializeActionExecutionResultJson(invalid) << "\n";
        } else {
            std::cerr << "Usage: iee act \"click export\" | --action <name> --target \"<label>\" [--value <text>] [--app <hint>] [--domain <hint>]\n";
        }
        return 1;
    }

    ActionExecutor executor(intentRegistry_, executionEngine_, telemetry_);
    const ActionExecutionResult result = executor.Act(request);

    if (jsonMode) {
        std::cout << SerializeActionExecutionResultJson(result) << "\n";
        return result.status == "success" ? 0 : 1;
    }

    std::cout << "Action request\n";
    std::cout << "  action           : " << request.action << "\n";
    std::cout << "  target           : " << request.target << "\n";
    if (!request.value.empty()) {
        std::cout << "  value            : " << request.value << "\n";
    }
    if (!request.context.app.empty()) {
        std::cout << "  app hint         : " << request.context.app << "\n";
    }
    if (!request.context.domain.empty()) {
        std::cout << "  domain hint      : " << request.context.domain << "\n";
    }

    std::cout << "\nAction result\n";
    std::cout << "  status           : " << result.status << "\n";
    std::cout << "  trace_id         : " << result.traceId << "\n";
    std::cout << "  resolved_node_id : " << result.resolvedNodeId << "\n";
    std::cout << "  reveal_used      : " << (result.revealUsed ? "true" : "false") << "\n";
    std::cout << "  verified         : " << (result.verified ? "true" : "false") << "\n";
    if (!result.reason.empty()) {
        std::cout << "  reason           : " << result.reason << "\n";
    }
    if (!result.executionStatus.empty()) {
        std::cout << "  execution_status : " << result.executionStatus << "\n";
    }
    if (!result.executionMethod.empty()) {
        std::cout << "  execution_method : " << result.executionMethod << "\n";
    }
    if (!result.executionMessage.empty()) {
        std::cout << "  execution_msg    : " << result.executionMessage << "\n";
    }

    if (result.status != "success" && !result.candidates.empty()) {
        std::cout << "\nCandidates\n";
        for (const ActionResolutionCandidate& candidate : result.candidates) {
            std::cout << "  - node=" << candidate.nodeId
                      << " confidence=" << std::fixed << std::setprecision(3) << candidate.confidence
                      << " label=\"" << candidate.label << "\"\n";
        }
    }

    return result.status == "success" ? 0 : 1;
}

int CliApp::HandleInspect(const ParsedCommand& command) {
    intentRegistry_.Refresh();

    const auto snapshot = intentRegistry_.LastSnapshot();
    const auto intents = intentRegistry_.ListIntents();
    const CapabilityGraph graph = intentRegistry_.Graph();

    if (WantsJson(command)) {
        std::ostringstream json;
        json << "{";
        json << "\"active_window\":\"" << EscapeJson(ToNarrow(snapshot.activeWindowTitle)) << "\",";
        json << "\"process_path\":\"" << EscapeJson(ToNarrow(snapshot.activeProcessPath)) << "\",";
        json << "\"snapshot_sequence\":" << snapshot.sequence << ",";
        json << "\"ui_elements\":" << snapshot.uiElements.size() << ",";
        json << "\"filesystem_entries\":" << snapshot.fileSystemEntries.size() << ",";
        json << "\"graph_nodes\":" << graph.Size() << ",";
        json << "\"intent_count\":" << intents.size();
        json << "}";
        std::cout << json.str() << "\n";
        return 0;
    }

    std::cout << "Active window : " << ToNarrow(snapshot.activeWindowTitle) << "\n";
    std::cout << "Process path  : " << ToNarrow(snapshot.activeProcessPath) << "\n";
    std::cout << "Cursor        : (" << snapshot.cursorPosition.x << ", " << snapshot.cursorPosition.y << ")\n";
    std::cout << "Snapshot seq  : " << snapshot.sequence << "\n";
    std::cout << "UI elements   : " << snapshot.uiElements.size() << "\n";
    std::cout << "FS entries    : " << snapshot.fileSystemEntries.size() << "\n";
    std::cout << "Graph nodes   : " << graph.Size() << "\n";
    std::cout << "Intent count  : " << intents.size() << "\n";
    return 0;
}

int CliApp::HandleGraph(const ParsedCommand& command) {
    ObserverSnapshot snapshot;
    const InteractionGraph graph = BuildLatestInteractionGraph(intentRegistry_, &snapshot);
    if (!snapshot.valid || !graph.valid) {
        std::cerr << "Unable to capture interaction graph from the current environment\n";
        return 1;
    }

    bool deltaRequested = false;
    std::uint64_t deltaSinceVersion = 0;

    std::string deltaSince = ReadOption(command, "delta_since");
    if (!deltaSince.empty()) {
        deltaRequested = true;
    }

    if (!deltaRequested && HasOption(command, "delta")) {
        deltaRequested = true;
        const std::string deltaValue = ReadOption(command, "delta");
        if (!deltaValue.empty() && deltaValue != "true") {
            deltaSince = deltaValue;
        }
    }

    if (!deltaSince.empty()) {
        const auto [ptr, error] = std::from_chars(
            deltaSince.data(),
            deltaSince.data() + deltaSince.size(),
            deltaSinceVersion);
        if (error != std::errc() || ptr != deltaSince.data() + deltaSince.size()) {
            deltaSinceVersion = 0;
        }
    }

    GraphDelta delta;
    if (deltaRequested) {
        if (deltaSinceVersion == 0) {
            InteractionGraph emptyGraph;
            delta = InteractionGraphBuilder::ComputeDelta(emptyGraph, graph);
            delta.fromVersion = 0;
            delta.toVersion = graph.version;
        } else if (deltaSinceVersion == graph.version) {
            delta.fromVersion = deltaSinceVersion;
            delta.toVersion = graph.version;
            delta.changed = false;
        } else {
            delta.fromVersion = deltaSinceVersion;
            delta.toVersion = graph.version;
            delta.changed = true;
            delta.resetRequired = true;
        }
    }

    if (WantsJson(command)) {
        if (!deltaRequested) {
            std::cout << InteractionGraphBuilder::SerializeGraphJson(graph) << "\n";
            return 0;
        }

        std::cout << "{";
        std::cout << "\"graph\":" << InteractionGraphBuilder::SerializeGraphJson(graph) << ",";
        std::cout << "\"delta_since\":" << deltaSinceVersion << ",";
        std::cout << "\"delta\":" << InteractionGraphBuilder::SerializeDeltaJson(delta);
        std::cout << "}" << "\n";
        return 0;
    }

    std::size_t hiddenCount = 0;
    std::size_t offscreenCount = 0;
    std::size_t collapsedCount = 0;
    for (const auto& entry : graph.nodes) {
        const InteractionNode& node = entry.second;
        hiddenCount += node.hidden ? 1U : 0U;
        offscreenCount += node.offscreen ? 1U : 0U;
        collapsedCount += node.collapsed ? 1U : 0U;
    }

    std::vector<std::string> sortedIds;
    sortedIds.reserve(graph.nodes.size());
    for (const auto& entry : graph.nodes) {
        sortedIds.push_back(entry.first);
    }
    std::sort(sortedIds.begin(), sortedIds.end());

    const std::size_t maxRows = ReadSizeOption(command, "limit", 20U, 500U);

    std::cout << "Interaction graph\n";
    std::cout << "  Sequence         : " << graph.sequence << "\n";
    std::cout << "  Version          : " << graph.version << "\n";
    std::cout << "  Signature        : " << graph.signature << "\n";
    std::cout << "  Nodes            : " << graph.nodes.size() << "\n";
    std::cout << "  Edges            : " << graph.edges.size() << "\n";
    std::cout << "  Commands         : " << graph.commands.size() << "\n";
    std::cout << "  Hidden           : " << hiddenCount << "\n";
    std::cout << "  Offscreen        : " << offscreenCount << "\n";
    std::cout << "  Collapsed        : " << collapsedCount << "\n";

    if (deltaRequested) {
        std::cout << "  Delta from       : " << delta.fromVersion << "\n";
        std::cout << "  Delta changed    : " << (delta.changed ? "true" : "false") << "\n";
        std::cout << "  Reset required   : " << (delta.resetRequired ? "true" : "false") << "\n";
        std::cout << "  Added nodes      : " << delta.addedNodes.size() << "\n";
        std::cout << "  Updated nodes    : " << delta.updatedNodes.size() << "\n";
        std::cout << "  Removed nodes    : " << delta.removedNodes.size() << "\n";
    }

    std::cout << "\nSample nodes\n";
    std::cout << std::left << std::setw(32) << "NODE_ID"
              << std::setw(12) << "TYPE"
              << std::setw(8) << "VIS"
              << std::setw(8) << "EN"
              << "LABEL\n";
    std::cout << std::string(80, '-') << "\n";

    const std::size_t rows = std::min<std::size_t>(maxRows, sortedIds.size());
    for (std::size_t index = 0; index < rows; ++index) {
        const auto it = graph.nodes.find(sortedIds[index]);
        if (it == graph.nodes.end()) {
            continue;
        }

        const InteractionNode& node = it->second;
        const std::string label = node.label.empty() ? "<unnamed>" : node.label;
        std::cout << std::left << std::setw(32) << node.id.substr(0, std::min<std::size_t>(31U, node.id.size()))
                  << std::setw(12) << node.type
                  << std::setw(8) << (node.visible ? "yes" : "no")
                  << std::setw(8) << (node.enabled ? "yes" : "no")
                  << label
                  << "\n";
    }

    if (rows < sortedIds.size()) {
        std::cout << "... " << (sortedIds.size() - rows) << " more nodes (use --limit N or --json).\n";
    }

    return 0;
}

int CliApp::HandleNode(const ParsedCommand& command) {
    const std::string nodeId = ResolveNodeIdArgument(command);

    if (nodeId.empty()) {
        std::cerr << "Usage: iee node <id> [--json]\n";
        return 1;
    }

    ObserverSnapshot snapshot;
    const InteractionGraph graph = BuildLatestInteractionGraph(intentRegistry_, &snapshot);
    if (!snapshot.valid || !graph.valid) {
        std::cerr << "Unable to capture interaction graph from the current environment\n";
        return 1;
    }

    const auto node = InteractionGraphBuilder::FindNode(graph, nodeId);
    if (!node.has_value()) {
        std::cerr << "Node not found: " << nodeId << "\n";
        return 1;
    }

    const Intent mappedIntent = InteractionGraphBuilder::GenerateIntent(*node);

    if (WantsJson(command)) {
        std::cout << "{";
        std::cout << "\"node\":" << InteractionGraphBuilder::SerializeNodeJson(*node) << ",";
        std::cout << "\"intent\":" << mappedIntent.Serialize() << ",";
        std::cout << "\"execution_plan\":" << InteractionGraphBuilder::SerializeExecutionPlanJson(node->executionPlan) << ",";
        std::cout << "\"reveal_strategy\":" << InteractionGraphBuilder::SerializeRevealStrategyJson(node->revealStrategy) << ",";
        std::cout << "\"intent_binding\":" << InteractionGraphBuilder::SerializeIntentBindingJson(node->intentBinding);
        std::cout << "}" << "\n";
        return 0;
    }

    std::cout << "Interaction node\n";
    std::cout << "  ID         : " << node->id << "\n";
    std::cout << "  Stable ID  : " << node->nodeId.stableId << "\n";
    std::cout << "  Node Sig   : " << node->nodeId.signature << "\n";
    std::cout << "  Type       : " << node->type << "\n";
    std::cout << "  Label      : " << (node->label.empty() ? "<unnamed>" : node->label) << "\n";
    std::cout << "  Parent     : " << (node->parentId.empty() ? "<root>" : node->parentId) << "\n";
    std::cout << "  Visible    : " << (node->visible ? "true" : "false") << "\n";
    std::cout << "  Enabled    : " << (node->enabled ? "true" : "false") << "\n";
    std::cout << "  Hidden     : " << (node->hidden ? "true" : "false") << "\n";
    std::cout << "  Offscreen  : " << (node->offscreen ? "true" : "false") << "\n";
    std::cout << "  Collapsed  : " << (node->collapsed ? "true" : "false") << "\n";
    std::cout << "  Shortcut   : " << (node->shortcut.empty() ? "<none>" : node->shortcut) << "\n";
    std::cout << "  Plan       : " << node->executionPlan.id << " ("
              << (node->executionPlan.executable ? "executable" : "non-executable") << ")\n";
    std::cout << "  Plan steps : " << node->executionPlan.steps.size() << "\n";
    std::cout << "  Reveal req : " << (node->revealStrategy.required ? "true" : "false") << "\n";
    std::cout << "  Reveal stp : " << node->revealStrategy.steps.size() << "\n";
    std::cout << "\nMapped intent\n";
    std::cout << "  Action     : " << ToString(mappedIntent.action) << "\n";
    std::cout << "  Source     : " << mappedIntent.source << "\n";
    std::cout << "  Confidence : " << std::fixed << std::setprecision(2) << mappedIntent.confidence << "\n";
    return 0;
}

int CliApp::HandlePlan(const ParsedCommand& command) {
    const std::string nodeId = ResolveNodeIdArgument(command);
    if (nodeId.empty()) {
        std::cerr << "Usage: iee plan <id> [--json]\n";
        return 1;
    }

    ObserverSnapshot snapshot;
    const InteractionGraph graph = BuildLatestInteractionGraph(intentRegistry_, &snapshot);
    if (!snapshot.valid || !graph.valid) {
        std::cerr << "Unable to capture interaction graph from the current environment\n";
        return 1;
    }

    const auto plan = InteractionGraphBuilder::GetExecutionPlan(graph, nodeId);
    if (!plan.has_value()) {
        std::cerr << "Node not found: " << nodeId << "\n";
        return 1;
    }

    if (WantsJson(command)) {
        std::cout << InteractionGraphBuilder::SerializeExecutionPlanJson(*plan) << "\n";
        return 0;
    }

    std::cout << "Execution plan\n";
    std::cout << "  Node       : " << nodeId << "\n";
    std::cout << "  Plan ID    : " << plan->id << "\n";
    std::cout << "  Executable : " << (plan->executable ? "true" : "false") << "\n";
    std::cout << "  Reason     : " << plan->reason << "\n";
    std::cout << "  Steps      : " << plan->steps.size() << "\n";

    if (!plan->steps.empty()) {
        std::cout << "\n";
        std::cout << std::left << std::setw(22) << "STEP_ID"
                  << std::setw(20) << "ACTION"
                  << std::setw(36) << "TARGET"
                  << "ARG\n";
        std::cout << std::string(98, '-') << "\n";

        for (const PlanStep& step : plan->steps) {
            std::cout << std::left << std::setw(22) << step.id.substr(0, std::min<std::size_t>(21U, step.id.size()))
                      << std::setw(20) << step.action
                      << std::setw(36) << step.targetId.substr(0, std::min<std::size_t>(35U, step.targetId.size()))
                      << (step.argument.empty() ? "<none>" : step.argument)
                      << "\n";
        }
    }

    return 0;
}

int CliApp::HandleReveal(const ParsedCommand& command) {
    const std::string nodeId = ResolveNodeIdArgument(command);
    if (nodeId.empty()) {
        std::cerr << "Usage: iee reveal <id> [--json]\n";
        return 1;
    }

    ObserverSnapshot snapshot;
    const InteractionGraph graph = BuildLatestInteractionGraph(intentRegistry_, &snapshot);
    if (!snapshot.valid || !graph.valid) {
        std::cerr << "Unable to capture interaction graph from the current environment\n";
        return 1;
    }

    const auto reveal = InteractionGraphBuilder::GetRevealStrategy(graph, nodeId);
    if (!reveal.has_value()) {
        std::cerr << "Node not found: " << nodeId << "\n";
        return 1;
    }

    if (WantsJson(command)) {
        std::cout << InteractionGraphBuilder::SerializeRevealStrategyJson(*reveal) << "\n";
        return 0;
    }

    std::cout << "Reveal strategy\n";
    std::cout << "  Node       : " << nodeId << "\n";
    std::cout << "  Required   : " << (reveal->required ? "true" : "false") << "\n";
    std::cout << "  Guaranteed : " << (reveal->guaranteed ? "true" : "false") << "\n";
    std::cout << "  Reason     : " << reveal->reason << "\n";
    std::cout << "  Steps      : " << reveal->steps.size() << "\n";

    if (!reveal->steps.empty()) {
        std::cout << "\n";
        std::cout << std::left << std::setw(22) << "STEP_ID"
                  << std::setw(24) << "ACTION"
                  << "TARGET\n";
        std::cout << std::string(82, '-') << "\n";

        for (const PlanStep& step : reveal->steps) {
            std::cout << std::left << std::setw(22) << step.id.substr(0, std::min<std::size_t>(21U, step.id.size()))
                      << std::setw(24) << step.action
                      << step.targetId
                      << "\n";
        }
    }

    return 0;
}

int CliApp::HandleCapabilities(const ParsedCommand& command) {
    if (!HasOption(command, "all")) {
        intentRegistry_.Refresh();
        const auto intents = intentRegistry_.ListIntents();

        if (WantsJson(command)) {
            std::cout << "[";
            for (std::size_t index = 0; index < intents.size(); ++index) {
                if (index > 0) {
                    std::cout << ",";
                }
                std::cout << intents[index].Serialize();
            }
            std::cout << "]\n";
            return 0;
        }

        std::map<std::string, std::size_t> actionCounts;
        for (const Intent& intent : intents) {
            ++actionCounts[ToString(intent.action)];
        }

        std::cout << "Capabilities (registered adapters)\n";
        std::cout << "  Total intents: " << intents.size() << "\n";
        for (const auto& entry : actionCounts) {
            std::cout << "  - " << entry.first << ": " << entry.second << "\n";
        }
        std::cout << "Use --all to include hidden/offscreen/collapsed graph nodes.\n";
        return 0;
    }

    ObserverSnapshot snapshot;
    const InteractionGraph graph = BuildLatestInteractionGraph(intentRegistry_, &snapshot);
    if (!snapshot.valid || !graph.valid) {
        std::cerr << "Unable to capture interaction graph from the current environment\n";
        return 1;
    }

    const std::size_t limit = ReadSizeOption(command, "limit", 4096U, 32768U);
    const std::vector<Intent> intents = InteractionGraphBuilder::GenerateIntents(graph, true, limit);

    if (WantsJson(command)) {
        std::cout << "{";
        std::cout << "\"count\":" << intents.size() << ",";
        std::cout << "\"capabilities\":[";
        for (std::size_t index = 0; index < intents.size(); ++index) {
            if (index > 0) {
                std::cout << ",";
            }
            std::cout << intents[index].Serialize();
        }
        std::cout << "]";
        std::cout << "}\n";
        return 0;
    }

    std::size_t hiddenCount = 0;
    for (const auto& entry : graph.nodes) {
        hiddenCount += entry.second.hidden ? 1U : 0U;
    }

    std::cout << "Capabilities (full interaction graph)\n";
    std::cout << "  Total capabilities : " << intents.size() << "\n";
    std::cout << "  Graph nodes        : " << graph.nodes.size() << "\n";
    std::cout << "  Hidden nodes       : " << hiddenCount << "\n";
    std::cout << "  Commands           : " << graph.commands.size() << "\n";

    const std::size_t rows = std::min<std::size_t>(20U, intents.size());
    if (rows > 0) {
        std::cout << "\nSample intents\n";
        std::cout << std::left << std::setw(12) << "ACTION"
                  << std::setw(36) << "NODE_ID"
                  << std::setw(10) << "SOURCE"
                  << "TARGET\n";
        std::cout << std::string(88, '-') << "\n";
        for (std::size_t index = 0; index < rows; ++index) {
            const Intent& intent = intents[index];
            std::cout << std::left << std::setw(12) << ToString(intent.action)
                      << std::setw(36) << intent.target.nodeId.substr(0, std::min<std::size_t>(35U, intent.target.nodeId.size()))
                      << std::setw(10) << intent.source
                      << ToNarrow(intent.target.label)
                      << "\n";
        }
        if (rows < intents.size()) {
            std::cout << "... " << (intents.size() - rows) << " more intents (use --json for full list).\n";
        }
    }

    return 0;
}

int CliApp::HandleExplain(const ParsedCommand& command) {
    const bool jsonMode = WantsJson(command);
    const ExplainInput input = ParseExplainInput(command);
    if (input.action == IntentAction::Unknown || input.target.empty()) {
        if (jsonMode) {
            std::cout << "{\"error\":{\"code\":\"invalid_explain_request\",\"message\":\"Usage: iee explain --action <intent> --target \\\"<label>\\\"\"}}\n";
        } else {
            std::cerr << "Usage: iee explain --action <intent> --target \"<label>\"\n";
        }
        return 1;
    }

    intentRegistry_.Refresh();

    const ResolutionResult result = intentRegistry_.Resolve(input.action, input.target);
    if (jsonMode) {
        std::cout << "{";
        std::cout << "\"action\":\"" << EscapeJson(ToString(input.action)) << "\",";
        std::cout << "\"target\":\"" << EscapeJson(ToNarrow(input.target)) << "\",";
        std::cout << "\"ambiguous\":" << (result.ambiguity.has_value() ? "true" : "false") << ",";
        std::cout << "\"candidates\":[";

        const std::size_t maxRows = std::min<std::size_t>(result.ranked.size(), 8U);
        for (std::size_t i = 0; i < maxRows; ++i) {
            if (i > 0) {
                std::cout << ",";
            }

            const auto& match = result.ranked[i];
            std::cout << "{";
            std::cout << "\"score\":" << match.score << ",";
            std::cout << "\"depth\":" << match.depthScore << ",";
            std::cout << "\"proximity\":" << match.proximityScore << ",";
            std::cout << "\"focus\":" << match.focusScore << ",";
            std::cout << "\"recency\":" << match.recencyScore << ",";
            std::cout << "\"intent\":" << match.intent.Serialize();
            std::cout << "}";
        }
        std::cout << "]";

        if (result.bestMatch.has_value()) {
            std::cout << ",\"best\":" << result.bestMatch->intent.Serialize();
        }

        std::cout << "}\n";
        return result.ranked.empty() ? 1 : 0;
    }

    std::cout << "Explain request: action=" << ToString(input.action)
              << " target=\"" << ToNarrow(input.target) << "\"\n";

    if (result.ranked.empty()) {
        std::cout << "No candidates found.\n";
        return 1;
    }

    if (result.ambiguity.has_value()) {
        std::cout << "Ambiguity detected: " << result.ambiguity->message << "\n";
    }

    std::cout << "Ranked candidates:\n";
    const std::size_t maxRows = std::min<std::size_t>(result.ranked.size(), 8U);
    for (std::size_t i = 0; i < maxRows; ++i) {
        const auto& match = result.ranked[i];
        std::cout << "  [" << (i + 1) << "] "
                  << ToString(match.intent.action)
                  << " target=\"" << ToNarrow(PrimaryTargetText(match.intent)) << "\""
                  << " score=" << std::fixed << std::setprecision(3) << match.score
                  << " (depth=" << match.depthScore
                  << ", proximity=" << match.proximityScore
                  << ", focus=" << match.focusScore
                  << ", recency=" << match.recencyScore
                  << ")\n";
    }

    if (result.bestMatch.has_value()) {
        std::cout << "Best match: " << result.bestMatch->intent.id << "\n";
    }

    return 0;
}

int CliApp::HandleDebugIntents(const ParsedCommand& command) {
    intentRegistry_.Refresh();
    const auto intents = intentRegistry_.ListIntents();

    if (WantsJson(command)) {
        std::cout << "[";
        for (std::size_t index = 0; index < intents.size(); ++index) {
            if (index > 0) {
                std::cout << ",";
            }
            std::cout << intents[index].Serialize();
        }
        std::cout << "]\n";
        return 0;
    }

    std::cout << "Debug intents (count=" << intents.size() << ")\n";
    for (const auto& intent : intents) {
        std::cout << "- " << intent.id
                  << " action=" << ToString(intent.action)
                  << " source=" << intent.source
                  << " target=\"" << ToNarrow(PrimaryTargetText(intent)) << "\""
                  << " confidence=" << std::fixed << std::setprecision(2) << intent.confidence
                  << " retries=" << intent.constraints.maxRetries
                  << " timeoutMs=" << intent.constraints.timeoutMs
                  << "\n";
    }

    return 0;
}

int CliApp::HandleApi(const ParsedCommand& command) {
    const std::uint16_t port = ReadPort(command, 8787);
    const bool singleRequest = HasOption(command, "once");

    std::cout << "Starting IEE local API on 127.0.0.1:" << port << "\n";
    std::cout << "Routes: GET /health, GET /intents, GET /capabilities, GET /control/status, "
                 "GET /capabilities/full, GET /interaction-graph, GET /interaction-node/{id}, "
                 "GET /telemetry/persistence, GET /trace/{trace_id}, GET /stream/state, GET /state/ai, GET /stream/frame, GET /stream/live, GET /perf, "
                 "GET /ure/status, GET /ure/goal, GET /ure/bundles, GET /ure/attention, GET /ure/prediction, GET /ure/skills, GET /ure/skills/active, GET /ure/anticipation, GET /ure/strategy, POST /execute, POST /act, POST /task/plan, POST /predict, POST /explain, POST /control/start, POST /control/stop, POST /ure/start, POST /ure/stop, POST /ure/goal, "
                 "POST /stream/control\n";
    std::cout << "Graph delta query: GET /interaction-graph?delta_since=<version>\n";
    if (singleRequest) {
        std::cout << "Mode: single request\n";
    }

    IntentApiServer api(intentRegistry_, executionEngine_, telemetry_);
    return api.Run(port, singleRequest);
}

int CliApp::HandleTelemetry(const ParsedCommand& command) {
    if (HasOption(command, "persistence")) {
        if (WantsJson(command)) {
            std::cout << telemetry_.SerializePersistenceJson() << "\n";
            return 0;
        }

        const TelemetryPersistenceStatus status = telemetry_.PersistenceStatus();
        std::cout << "Telemetry persistence\n";
        std::cout << "  Enabled            : " << (status.enabled ? "true" : "false") << "\n";
        std::cout << "  Queued             : " << status.queuedCount << "\n";
        std::cout << "  Persisted          : " << status.persistedCount << "\n";
        std::cout << "  Dropped            : " << status.droppedCount << "\n";
        std::cout << "  Current file index : " << status.currentFileIndex << "\n";

        if (!status.files.empty()) {
            std::cout << "  Files\n";
            for (const auto& file : status.files) {
                std::cout << "    - " << file << "\n";
            }
        }

        return 0;
    }

    const std::string statusFilter = ReadOption(command, "status");
    const std::string adapterFilter = ReadOption(command, "adapter");
    const std::size_t limit = ReadSizeOption(command, "limit", 20U, 500U);

    if (!statusFilter.empty() || !adapterFilter.empty()) {
        const auto traces = telemetry_.QueryExecutions(limit, statusFilter, adapterFilter);

        if (WantsJson(command)) {
            std::cout << "[";
            for (std::size_t index = 0; index < traces.size(); ++index) {
                if (index > 0) {
                    std::cout << ",";
                }
                std::cout << telemetry_.SerializeTraceJson(traces[index].traceId);
            }
            std::cout << "]\n";
            return 0;
        }

        std::cout << std::left << std::setw(40) << "TRACE_ID"
                  << std::setw(12) << "INTENT"
                  << std::setw(16) << "ADAPTER"
                  << std::setw(10) << "STATUS"
                  << "DURATION_MS\n";
        std::cout << std::string(96, '-') << "\n";

        for (const auto& trace : traces) {
            std::cout << std::left << std::setw(40) << trace.traceId
                      << std::setw(12) << trace.intent
                      << std::setw(16) << trace.adapter
                      << std::setw(10) << trace.status
                      << trace.durationMs
                      << "\n";
        }

        std::cout << "\nFiltered traces: " << traces.size() << "\n";
        return 0;
    }

    if (WantsJson(command)) {
        std::cout << telemetry_.SerializeSnapshotJson() << "\n";
        return 0;
    }

    const TelemetrySnapshot snapshot = telemetry_.Snapshot();

    std::cout << "Telemetry summary\n";
    std::cout << "  Total executions   : " << snapshot.totalExecutions << "\n";
    std::cout << "  Successes          : " << snapshot.successCount << "\n";
    std::cout << "  Failures           : " << snapshot.failureCount << "\n";
    std::cout << "  Success rate       : " << std::fixed << std::setprecision(2) << (snapshot.successRate * 100.0) << "%\n";
    std::cout << "  Avg latency (ms)   : " << std::fixed << std::setprecision(2) << snapshot.averageLatencyMs << "\n";
    std::cout << "  Avg resolve (ms)   : " << std::fixed << std::setprecision(2) << snapshot.averageResolutionMs << "\n";
    std::cout << "  Uptime (ms)        : " << snapshot.uptimeMs << "\n";

    if (!snapshot.adapterMetrics.empty()) {
        std::cout << "\nPer-adapter metrics\n";
        std::cout << std::left << std::setw(18) << "ADAPTER"
                  << std::setw(12) << "EXECUTIONS"
                  << std::setw(12) << "SUCCESS%"
                  << "AVG_MS\n";
        std::cout << std::string(54, '-') << "\n";

        for (const auto& metrics : snapshot.adapterMetrics) {
            std::cout << std::left << std::setw(18) << metrics.adapter
                      << std::setw(12) << metrics.executions
                      << std::setw(12) << std::fixed << std::setprecision(2) << (metrics.successRate * 100.0)
                      << std::fixed << std::setprecision(2) << metrics.averageLatencyMs
                      << "\n";
        }
    }

    return 0;
}

int CliApp::HandleTrace(const ParsedCommand& command) {
    const bool jsonMode = WantsJson(command);
    std::string traceId = ReadOption(command, "id");
    if (traceId.empty() && !command.positionals.empty()) {
        traceId = command.positionals[0];
    }

    if (!traceId.empty()) {
        std::cout << telemetry_.SerializeTraceJson(traceId) << "\n";
        const auto trace = telemetry_.FindTrace(traceId);
        return trace.has_value() ? 0 : 1;
    }

    const std::size_t limit = ReadSizeOption(command, "limit", 20U, 500U);
    const auto traces = telemetry_.RecentExecutions(limit);
    if (traces.empty()) {
        if (jsonMode) {
            std::cout << "[]\n";
        } else {
            std::cout << "No traces recorded yet.\n";
        }
        return 0;
    }

    if (jsonMode) {
        std::cout << "[";
        for (std::size_t index = 0; index < traces.size(); ++index) {
            if (index > 0) {
                std::cout << ",";
            }
            std::cout << telemetry_.SerializeTraceJson(traces[index].traceId);
        }
        std::cout << "]\n";
        return 0;
    }

    std::cout << std::left << std::setw(40) << "TRACE_ID"
              << std::setw(12) << "INTENT"
              << std::setw(16) << "ADAPTER"
              << std::setw(10) << "STATUS"
              << "DURATION_MS\n";
    std::cout << std::string(96, '-') << "\n";

    for (const auto& trace : traces) {
        std::cout << std::left << std::setw(40) << trace.traceId
                  << std::setw(12) << trace.intent
                  << std::setw(16) << trace.adapter
                  << std::setw(10) << trace.status
                  << trace.durationMs
                  << "\n";
    }

    return 0;
}

int CliApp::HandleLatency(const ParsedCommand& command) {
    const std::size_t limit = ReadSizeOption(command, "limit", 200U, 4096U);

    if (WantsJson(command)) {
        std::cout << telemetry_.SerializeLatencyJson(limit) << "\n";
        return 0;
    }

    const LatencyBreakdownSnapshot snapshot = telemetry_.LatencySnapshot(limit);
    std::cout << "Latency breakdown (samples=" << snapshot.sampleCount << ")\n";
    std::cout << std::left << std::setw(14) << "PHASE"
              << std::setw(12) << "AVG_MS"
              << std::setw(12) << "P95_MS"
              << "MAX_MS\n";
    std::cout << std::string(50, '-') << "\n";

    const auto printRow = [](const char* label, const LatencyComponentStats& stats) {
        std::cout << std::left << std::setw(14) << label
                  << std::setw(12) << std::fixed << std::setprecision(3) << stats.averageMs
                  << std::setw(12) << std::fixed << std::setprecision(3) << stats.p95Ms
                  << std::fixed << std::setprecision(3) << stats.maxMs << "\n";
    };

    printRow("observation", snapshot.observation);
    printRow("perception", snapshot.perception);
    printRow("queue_wait", snapshot.queueWait);
    printRow("execution", snapshot.execution);
    printRow("verification", snapshot.verification);
    printRow("total", snapshot.total);

    if (snapshot.latest.has_value()) {
        const LatencyBreakdownSample& latest = *snapshot.latest;
        std::cout << "\nLatest sample\n";
        std::cout << "  Frame           : " << latest.frame << "\n";
        std::cout << "  Trace ID        : " << latest.traceId << "\n";
        std::cout << "  Observation (ms): " << std::fixed << std::setprecision(3) << latest.observationMs << "\n";
        std::cout << "  Perception  (ms): " << std::fixed << std::setprecision(3) << latest.perceptionMs << "\n";
        std::cout << "  Queue wait  (ms): " << std::fixed << std::setprecision(3) << latest.queueWaitMs << "\n";
        std::cout << "  Execution   (ms): " << std::fixed << std::setprecision(3) << latest.executionMs << "\n";
        std::cout << "  Total       (ms): " << std::fixed << std::setprecision(3) << latest.totalMs << "\n";
    }

    return 0;
}

int CliApp::HandlePerf(const ParsedCommand& command) {
    const std::size_t limit = ReadSizeOption(command, "limit", 200U, 4096U);
    const double targetBudgetMs = ReadDoubleOption(command, "target_ms", 16.0, 1.0, 1000.0);
    const bool strict = HasOption(command, "strict");
    const PerformanceContractSnapshot contract = telemetry_.PerformanceContract(targetBudgetMs, limit);

    if (WantsJson(command)) {
        std::ostringstream json;
        json << "{";
        json << "\"strict\":" << (strict ? "true" : "false") << ",";
        json << "\"strict_passed\":" << (contract.withinBudget ? "true" : "false") << ",";
        json << "\"strict_status\":\""
             << (strict ? (contract.withinBudget ? "pass" : "fail") : "disabled")
             << "\",";
        json << "\"contract\":" << telemetry_.SerializePerformanceContractJson(targetBudgetMs, limit);
        json << "}";

        std::cout << json.str() << "\n";
        return (strict && !contract.withinBudget) ? 2 : 0;
    }

    std::cout << "Performance contract\n";
    std::cout << "  Samples          : " << contract.sampleCount << "\n";
    std::cout << "  Target budget ms : " << std::fixed << std::setprecision(3) << contract.targetBudgetMs << "\n";
    std::cout << "  p50 total ms     : " << std::fixed << std::setprecision(3) << contract.p50Ms << "\n";
    std::cout << "  p95 total ms     : " << std::fixed << std::setprecision(3) << contract.p95Ms << "\n";
    std::cout << "  max total ms     : " << std::fixed << std::setprecision(3) << contract.maxMs << "\n";
    std::cout << "  jitter ms        : " << std::fixed << std::setprecision(3) << contract.jitterMs << "\n";
    std::cout << "  drift ms         : " << std::fixed << std::setprecision(3) << contract.driftMs << "\n";
    std::cout << "  within budget    : " << (contract.withinBudget ? "true" : "false") << "\n";

    if (strict) {
        std::cout << "  strict mode      : " << (contract.withinBudget ? "pass" : "fail") << "\n";
    } else {
        std::cout << "  strict mode      : disabled\n";
    }

    return (strict && !contract.withinBudget) ? 2 : 0;
}

int CliApp::HandleVision(const ParsedCommand& command) {
    const std::size_t limit = ReadSizeOption(command, "limit", 200U, 4096U);

    if (WantsJson(command)) {
        std::cout << telemetry_.SerializeVisionJson(limit) << "\n";
        return 0;
    }

    const VisionSnapshot snapshot = telemetry_.VisionLatencySnapshot(limit);

    std::cout << "Vision latency\n";
    std::cout << "  Samples           : " << snapshot.sampleCount << "\n";
    std::cout << "  Simulated samples : " << snapshot.simulatedSamples << "\n";
    std::cout << "  Dropped frames    : " << snapshot.droppedFrames << "\n";
    std::cout << "  Estimated FPS     : " << std::fixed << std::setprecision(3) << snapshot.estimatedFps << "\n";

    std::cout << "\n";
    std::cout << std::left << std::setw(12) << "PHASE"
              << std::setw(12) << "AVG_MS"
              << std::setw(12) << "P95_MS"
              << "MAX_MS\n";
    std::cout << std::string(48, '-') << "\n";

    const auto printRow = [](const char* phase, const VisionComponentStats& stats) {
        std::cout << std::left << std::setw(12) << phase
                  << std::setw(12) << std::fixed << std::setprecision(3) << stats.averageMs
                  << std::setw(12) << std::fixed << std::setprecision(3) << stats.p95Ms
                  << std::fixed << std::setprecision(3) << stats.maxMs << "\n";
    };

    printRow("capture", snapshot.capture);
    printRow("detect", snapshot.detection);
    printRow("merge", snapshot.merge);
    printRow("total", snapshot.total);

    if (snapshot.latest.has_value()) {
        const VisionLatencySample& latest = *snapshot.latest;
        std::cout << "\nLatest frame\n";
        std::cout << "  Frame ID         : " << latest.frameId << "\n";
        std::cout << "  Env sequence     : " << latest.environmentSequence << "\n";
        std::cout << "  Capture ms       : " << std::fixed << std::setprecision(3) << latest.captureMs << "\n";
        std::cout << "  Detection ms     : " << std::fixed << std::setprecision(3) << latest.detectionMs << "\n";
        std::cout << "  Merge ms         : " << std::fixed << std::setprecision(3) << latest.mergeMs << "\n";
        std::cout << "  Total ms         : " << std::fixed << std::setprecision(3) << latest.totalMs << "\n";
        std::cout << "  Simulated        : " << (latest.simulated ? "true" : "false") << "\n";
    }

    return 0;
}

int CliApp::HandleUre(const ParsedCommand& command) {
    const bool jsonMode = WantsJson(command);
    if (command.positionals.empty()) {
        if (jsonMode) {
            std::cout << "{\"error\":{\"code\":\"missing_ure_subcommand\",\"message\":\"Usage: iee ure live|debug|skills|anticipation|strategy|demo realtime\"}}\n";
        } else {
            std::cerr << "Usage: iee ure live|debug|skills|anticipation|strategy|demo realtime\n";
        }
        return 1;
    }

    const std::string subcommand = ToAsciiLower(command.positionals[0]);
    const bool demoRealtime = subcommand == "demo" && command.positionals.size() > 1U &&
        ToAsciiLower(command.positionals[1]) == "realtime";
    const bool skillsQuery = subcommand == "skills";
    const bool anticipationQuery = subcommand == "anticipation";
    const bool strategyQuery = subcommand == "strategy";

    if (subcommand != "live" && subcommand != "debug" && !skillsQuery && !anticipationQuery && !strategyQuery && !demoRealtime) {
        if (jsonMode) {
            std::cout << "{\"error\":{\"code\":\"invalid_ure_subcommand\",\"message\":\"Usage: iee ure live|debug|skills|anticipation|strategy|demo realtime\"}}\n";
        } else {
            std::cerr << "Usage: iee ure live|debug|skills|anticipation|strategy|demo realtime\n";
        }
        return 1;
    }

    IntentApiServer api(intentRegistry_, executionEngine_, telemetry_);
    const auto request = [&api](const std::string& method, const std::string& path, const std::string& payload) {
        const std::string response = api.HandleRequestForTesting(BuildHttpRequest(method, path, payload));
        return std::make_pair(ParseHttpStatus(response), ExtractHttpBody(response));
    };

    if (skillsQuery) {
        const bool activeOnly = HasOption(command, "active");
        const std::size_t limit = ReadSizeOption(command, "limit", 8U, 64U);
        const std::string path = activeOnly
            ? "/ure/skills/active"
            : "/ure/skills?limit=" + std::to_string(limit);

        const auto [statusCode, body] = request("GET", path, "");
        if (jsonMode) {
            std::cout << "{";
            std::cout << "\"status_code\":" << statusCode << ",";
            std::cout << "\"payload\":" << (body.empty() ? "{}" : body);
            std::cout << "}\n";
        } else {
            std::cout << "URE skills (" << statusCode << ")\n";
            std::cout << body << "\n";
        }
        return (statusCode >= 200 && statusCode < 300) ? 0 : 1;
    }

    if (anticipationQuery) {
        const auto [statusCode, body] = request("GET", "/ure/anticipation", "");
        if (jsonMode) {
            std::cout << "{";
            std::cout << "\"status_code\":" << statusCode << ",";
            std::cout << "\"anticipation\":" << (body.empty() ? "{}" : body);
            std::cout << "}\n";
        } else {
            std::cout << "URE anticipation (" << statusCode << ")\n";
            std::cout << body << "\n";
        }
        return (statusCode >= 200 && statusCode < 300) ? 0 : 1;
    }

    if (strategyQuery) {
        const auto [statusCode, body] = request("GET", "/ure/strategy", "");
        if (jsonMode) {
            std::cout << "{";
            std::cout << "\"status_code\":" << statusCode << ",";
            std::cout << "\"strategy\":" << (body.empty() ? "{}" : body);
            std::cout << "}\n";
        } else {
            std::cout << "URE strategy (" << statusCode << ")\n";
            std::cout << body << "\n";
        }
        return (statusCode >= 200 && statusCode < 300) ? 0 : 1;
    }

    if (subcommand == "debug") {
        const bool includeBundles = HasOption(command, "bundles");
        const bool includeContinuous = HasOption(command, "continuous");

        const auto [statusCode, statusBody] = request("GET", "/ure/status", "");
        const auto [metricsCode, metricsBody] = request("GET", "/ure/metrics", "");
        const auto [bundlesCode, bundlesBody] = includeBundles
            ? request("GET", "/ure/bundles", "")
            : std::make_pair(0, std::string());
        const auto [attentionCode, attentionBody] = includeContinuous
            ? request("GET", "/ure/attention", "")
            : std::make_pair(0, std::string());
        const auto [predictionCode, predictionBody] = includeContinuous
            ? request("GET", "/ure/prediction", "")
            : std::make_pair(0, std::string());

        if (jsonMode) {
            std::cout << "{";
            std::cout << "\"status_code\":" << statusCode << ",";
            std::cout << "\"status\":" << (statusBody.empty() ? "{}" : statusBody) << ",";
            std::cout << "\"metrics_code\":" << metricsCode << ",";
            std::cout << "\"metrics\":" << (metricsBody.empty() ? "{}" : metricsBody);
            if (includeBundles) {
                std::cout << ",\"bundles_code\":" << bundlesCode << ",";
                std::cout << "\"bundles\":" << (bundlesBody.empty() ? "{}" : bundlesBody);
            }
            if (includeContinuous) {
                std::cout << ",\"attention_code\":" << attentionCode << ",";
                std::cout << "\"attention\":" << (attentionBody.empty() ? "{}" : attentionBody) << ",";
                std::cout << "\"prediction_code\":" << predictionCode << ",";
                std::cout << "\"prediction\":" << (predictionBody.empty() ? "[]" : predictionBody);
            }
            std::cout << "}\n";
        } else {
            std::cout << "URE status (" << statusCode << ")\n";
            std::cout << statusBody << "\n\n";
            std::cout << "URE metrics (" << metricsCode << ")\n";
            std::cout << metricsBody << "\n";

            if (includeBundles) {
                std::cout << "\nURE bundles (" << bundlesCode << ")\n";
                std::cout << bundlesBody << "\n";
            }

            if (includeContinuous) {
                std::cout << "\nURE attention (" << attentionCode << ")\n";
                std::cout << attentionBody << "\n\n";
                std::cout << "URE prediction (" << predictionCode << ")\n";
                std::cout << predictionBody << "\n";
            }
        }

        const bool baseOk = statusCode >= 200 && statusCode < 300 && metricsCode >= 200 && metricsCode < 300;
        const bool bundlesOk = !includeBundles || (bundlesCode >= 200 && bundlesCode < 300);
        const bool continuousOk = !includeContinuous ||
            ((attentionCode >= 200 && attentionCode < 300) && (predictionCode >= 200 && predictionCode < 300));
        return (baseOk && bundlesOk && continuousOk) ? 0 : 1;
    }

    const std::size_t samples = ReadSizeOption(command, "samples", demoRealtime ? 40U : 24U, 240U);
    const int intervalMs = static_cast<int>(ReadSizeOption(command, "interval_ms", demoRealtime ? 100U : 150U, 2000U));
    const bool executeActions = IsTruthyString(ReadOption(command, "execute"), !demoRealtime);
    const std::string priority = ReadOption(command, "priority").empty() ? "auto" : ReadOption(command, "priority");

    std::string startPayload = "{\"execute\":\"" + std::string(executeActions ? "true" : "false") + "\"," +
        "\"priority\":\"" + EscapeJson(priority) + "\"";

    const std::string decisionBudgetUs = ReadOption(command, "decision_budget_us");
    if (!decisionBudgetUs.empty()) {
        startPayload += ",\"decision_budget_us\":\"" + EscapeJson(decisionBudgetUs) + "\"";
    }

    const std::string targetFrameMs = ReadOption(command, "target_frame_ms");
    if (!targetFrameMs.empty()) {
        startPayload += ",\"targetFrameMs\":\"" + EscapeJson(targetFrameMs) + "\"";
    }

    if (demoRealtime) {
        startPayload += ",\"demo_mode\":\"true\"";

        const std::string goal = ReadOption(command, "goal").empty()
            ? "stabilize active interaction target"
            : ReadOption(command, "goal");
        const std::string target = ReadOption(command, "target");
        const std::string domain = ReadOption(command, "domain").empty() ? "generic" : ReadOption(command, "domain");

        std::ostringstream goalPayload;
        goalPayload << "{";
        goalPayload << "\"goal\":\"" << EscapeJson(goal) << "\",";
        goalPayload << "\"target\":\"" << EscapeJson(target) << "\",";
        goalPayload << "\"domain\":\"" << EscapeJson(domain) << "\",";
        goalPayload << "\"preferred_actions\":\"activate,select\",";
        goalPayload << "\"active\":\"true\"";
        goalPayload << "}";

        const auto [goalCode, goalBody] = request("POST", "/ure/goal", goalPayload.str());
        if (goalCode < 200 || goalCode >= 300) {
            if (jsonMode) {
                std::cout << "{\"error\":{\"code\":\"ure_goal_failed\",\"status\":" << goalCode
                          << ",\"body\":" << (goalBody.empty() ? "{}" : goalBody) << "}}\n";
            } else {
                std::cerr << "Failed to set URE goal (" << goalCode << ")\n" << goalBody << "\n";
            }
            return 1;
        }
    }

    startPayload += "}";

    const auto [startCode, startBody] = request("POST", "/ure/start", startPayload);
    if (startCode < 200 || startCode >= 300) {
        if (jsonMode) {
            std::cout << "{\"error\":{\"code\":\"ure_start_failed\",\"status\":" << startCode
                      << ",\"body\":" << (startBody.empty() ? "{}" : startBody) << "}}\n";
        } else {
            std::cerr << "Failed to start URE runtime (" << startCode << ")\n" << startBody << "\n";
        }
        return 1;
    }

    std::vector<std::string> sampleBodies;
    sampleBodies.reserve(samples);

    for (std::size_t index = 0; index < samples; ++index) {
        const auto [sampleCode, sampleBody] = request("GET", "/ure/status", "");
        int demoCode = 0;
        std::string demoBody;
        if (demoRealtime) {
            std::ostringstream demoPayload;
            demoPayload << "{";
            demoPayload << "\"scenario\":\"realtime\",";
            demoPayload << "\"execute\":\"false\"";
            demoPayload << "}";
            const auto demoResponse = request("POST", "/ure/demo", demoPayload.str());
            demoCode = demoResponse.first;
            demoBody = demoResponse.second;
        }

        if (sampleCode >= 200 && sampleCode < 300) {
            if (demoRealtime) {
                std::ostringstream combined;
                combined << "{";
                combined << "\"status\":" << (sampleBody.empty() ? "{}" : sampleBody) << ",";
                combined << "\"demo_code\":" << demoCode << ",";
                combined << "\"demo\":" << (demoBody.empty() ? "{}" : demoBody);
                combined << "}";
                sampleBodies.push_back(combined.str());
            } else {
                sampleBodies.push_back(sampleBody);
            }
        }

        if (!jsonMode) {
            std::cout << "URE sample " << (index + 1U) << "/" << samples << " (" << sampleCode << ")\n";
            if (demoRealtime) {
                std::cout << "Status:\n" << sampleBody << "\n";
                std::cout << "Demo (" << demoCode << "):\n" << demoBody << "\n";
            } else {
                std::cout << sampleBody << "\n";
            }
        }

        if (index + 1U < samples) {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }

    const auto [metricsCode, metricsBody] = request("GET", "/ure/metrics", "");
    const auto [stopCode, stopBody] = request("POST", "/ure/stop", "{}");

    if (jsonMode) {
        std::cout << "{";
        std::cout << "\"start\":" << (startBody.empty() ? "{}" : startBody) << ",";
        std::cout << "\"samples\":[";
        for (std::size_t index = 0; index < sampleBodies.size(); ++index) {
            if (index > 0) {
                std::cout << ",";
            }
            std::cout << sampleBodies[index];
        }
        std::cout << "],";
        std::cout << "\"metrics_code\":" << metricsCode << ",";
        std::cout << "\"metrics\":" << (metricsBody.empty() ? "{}" : metricsBody) << ",";
        std::cout << "\"stop_code\":" << stopCode << ",";
        std::cout << "\"stop\":" << (stopBody.empty() ? "{}" : stopBody);
        std::cout << "}\n";
    } else {
        std::cout << "URE metrics (" << metricsCode << ")\n";
        std::cout << metricsBody << "\n\n";
        std::cout << "URE stop (" << stopCode << ")\n";
        std::cout << stopBody << "\n";
    }

    return (metricsCode >= 200 && metricsCode < 300 && stopCode >= 200 && stopCode < 300) ? 0 : 1;
}

int CliApp::HandleDemo(const ParsedCommand& command) {
    std::string scenario = ReadOption(command, "scenario");
    if (scenario.empty() && !command.positionals.empty()) {
        scenario = command.positionals.front();
    }

    std::transform(scenario.begin(), scenario.end(), scenario.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });

    if (scenario != "presentation" && scenario != "browser") {
        std::cerr << "Usage: iee demo presentation|browser [--json] [--run]\n";
        return 1;
    }

    InteractionGraph graph = BuildLatestInteractionGraph(intentRegistry_);
    if (!graph.valid) {
        std::cerr << "Unable to build interaction graph for demo scenario\n";
        return 1;
    }

    TaskRequest request;
    if (scenario == "presentation") {
        request.goal = "start presentation mode";
        request.targetHint = "presentation slide show present";
        request.domain = TaskDomain::Presentation;
    } else {
        request.goal = "focus browser address bar";
        request.targetHint = "browser tab address search url";
        request.domain = TaskDomain::Browser;
    }
    request.allowHidden = true;
    request.maxPlans = 3;

    TaskPlanner planner;
    const TaskPlanResult plan = planner.Plan(request, graph);

    if (WantsJson(command)) {
        std::cout << TaskPlanner::SerializeJson(plan) << "\n";
    } else {
        std::cout << "Demo scenario: " << scenario << "\n";
        std::cout << "Task: " << plan.goal << "\n";
        std::cout << "Summary: " << plan.summary << "\n";
        std::cout << "Candidates: " << plan.candidates.size() << "\n";

        for (std::size_t index = 0; index < plan.candidates.size(); ++index) {
            const TaskPlanCandidate& candidate = plan.candidates[index];
            std::cout << "  [" << index << "] node=" << candidate.nodeId
                      << " action=" << candidate.action
                      << " score=" << std::fixed << std::setprecision(3) << candidate.score
                      << " hidden=" << (candidate.hidden ? "true" : "false")
                      << " reveal=" << (candidate.requiresReveal ? "true" : "false")
                      << " label=\"" << candidate.label << "\"\n";
        }
    }

    if (!HasOption(command, "run")) {
        return plan.candidates.empty() ? 1 : 0;
    }

    if (plan.candidates.empty()) {
        std::cerr << "No executable candidate available for demo run\n";
        return 1;
    }

    const std::string nodeId = plan.candidates.front().nodeId;
    const auto node = InteractionGraphBuilder::FindNode(graph, nodeId);
    if (!node.has_value()) {
        std::cerr << "Demo candidate node not found in graph\n";
        return 1;
    }

    Intent intent = InteractionGraphBuilder::GenerateIntent(*node);
    intent.source = "demo";

    ExecutionContract contract(executionEngine_, intentRegistry_);
    const ExecutionContractResult execution = contract.Execute(intent, nodeId);

    if (WantsJson(command)) {
        std::cout << "{";
        std::cout << "\"status\":\"" << EscapeJson(ToString(execution.execution.status)) << "\",";
        std::cout << "\"contract_satisfied\":" << (execution.contractSatisfied ? "true" : "false") << ",";
        std::cout << "\"stage\":\"" << EscapeJson(execution.stage) << "\",";
        std::cout << "\"message\":\"" << EscapeJson(execution.execution.message) << "\"";
        std::cout << "}\n";
        return execution.contractSatisfied ? 0 : 2;
    }

    std::cout << "Run result\n";
    std::cout << "  status            : " << ToString(execution.execution.status) << "\n";
    std::cout << "  contract_satisfied: " << (execution.contractSatisfied ? "true" : "false") << "\n";
    std::cout << "  stage             : " << execution.stage << "\n";
    std::cout << "  message           : " << execution.execution.message << "\n";

    return execution.contractSatisfied ? 0 : 2;
}

}  // namespace iee
