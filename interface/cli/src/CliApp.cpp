#include "CliApp.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <system_error>

#include "InteractionGraph.h"
#include "Intent.h"
#include "IntentApiServer.h"

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
    const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc() || ptr != value.data() + value.size() || parsed > std::numeric_limits<std::uint16_t>::max()) {
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

}  // namespace

CliApp::CliApp(IntentRegistry& intentRegistry, ExecutionEngine& executionEngine, Telemetry& telemetry)
    : intentRegistry_(intentRegistry), executionEngine_(executionEngine), telemetry_(telemetry) {}

int CliApp::Run(int argc, char* argv[]) {
    const ParsedCommand command = CliParser::Parse(argc, argv);

    if (command.command.empty()) {
        CliParser::PrintHelp();
        return 1;
    }

    if (command.command == "list-intents") {
        return HandleListIntents();
    }

    if (command.command == "execute") {
        return HandleExecute(command);
    }

    if (command.command == "inspect") {
        return HandleInspect();
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

    CliParser::PrintHelp();
    return 1;
}

int CliApp::HandleListIntents() {
    intentRegistry_.Refresh();
    const auto intents = intentRegistry_.ListIntents();

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
    const IntentAction action = IntentActionFromString(command.action);
    if (action == IntentAction::Unknown) {
        std::cerr << "Unknown intent action: " << command.action << "\n";
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
            std::cerr << "Missing --target for UI intent\n";
            return 1;
        }

        const std::wstring wideTarget = ToWide(target);
        const ResolutionResult resolution = intentRegistry_.Resolve(action, wideTarget);
        if (resolution.ambiguity.has_value()) {
            std::cerr << "Ambiguous target: " << target << "\n";
            for (const auto& candidate : resolution.ambiguity->candidates) {
                std::cerr << "  - " << ToString(candidate.intent.action)
                          << " target=\"" << ToNarrow(PrimaryTargetText(candidate.intent))
                          << "\" score=" << std::fixed << std::setprecision(3) << candidate.score
                          << "\n";
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
                std::cerr << "Missing --value for set_value\n";
                return 1;
            }
            intent.params.values["value"] = ToWide(value);
        }
    } else {
        intent.target.type = TargetType::FileSystemPath;
        if (action == IntentAction::Create) {
            const std::string path = ReadOption(command, "path");
            if (path.empty()) {
                std::cerr << "Missing --path for create\n";
                return 1;
            }
            intent.params.values["path"] = ToWide(path);
            intent.target.path = intent.params.values["path"];
            intent.target.label = intent.params.values["path"];
        } else if (action == IntentAction::Delete) {
            const std::string path = ReadOption(command, "path");
            if (path.empty()) {
                std::cerr << "Missing --path for delete\n";
                return 1;
            }
            intent.params.values["path"] = ToWide(path);
            intent.target.path = intent.params.values["path"];
            intent.target.label = intent.params.values["path"];
        } else if (action == IntentAction::Move) {
            const std::string path = ReadOption(command, "path");
            const std::string destination = ReadOption(command, "destination");
            if (path.empty() || destination.empty()) {
                std::cerr << "Missing --path or --destination for move\n";
                return 1;
            }
            intent.params.values["path"] = ToWide(path);
            intent.params.values["destination"] = ToWide(destination);
            intent.target.path = intent.params.values["path"];
            intent.target.label = intent.params.values["path"];
        }
    }

    const ExecutionResult result = executionEngine_.Execute(intent);
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

int CliApp::HandleInspect() {
    intentRegistry_.Refresh();

    const auto snapshot = intentRegistry_.LastSnapshot();
    const auto intents = intentRegistry_.ListIntents();
    const CapabilityGraph graph = intentRegistry_.Graph();

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

    if (HasOption(command, "json")) {
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

    if (HasOption(command, "json")) {
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

    if (HasOption(command, "json")) {
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

    if (HasOption(command, "json")) {
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

        if (HasOption(command, "json")) {
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

    if (HasOption(command, "json")) {
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
    const ExplainInput input = ParseExplainInput(command);
    if (input.action == IntentAction::Unknown || input.target.empty()) {
        std::cerr << "Usage: iee explain --action <intent> --target \"<label>\"\n";
        return 1;
    }

    intentRegistry_.Refresh();

    const ResolutionResult result = intentRegistry_.Resolve(input.action, input.target);
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

    if (HasOption(command, "json")) {
        std::cout << "\nSerialized intents:\n";
        for (const auto& intent : intents) {
            std::cout << intent.Serialize() << "\n";
        }
    }

    return 0;
}

int CliApp::HandleApi(const ParsedCommand& command) {
    const std::uint16_t port = ReadPort(command, 8787);
    const bool singleRequest = HasOption(command, "once");

    std::cout << "Starting IEE local API on 127.0.0.1:" << port << "\n";
    std::cout << "Routes: GET /health, GET /intents, GET /capabilities, GET /control/status, "
                 "GET /capabilities/full, GET /interaction-graph, GET /interaction-node/{id}, "
                 "GET /telemetry/persistence, GET /stream/state, GET /stream/frame, GET /stream/live, GET /perf, "
                 "POST /execute, POST /predict, POST /explain, POST /control/start, POST /control/stop, "
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
        if (HasOption(command, "json")) {
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

        if (HasOption(command, "json")) {
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

    if (HasOption(command, "json")) {
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
        std::cout << "No traces recorded yet.\n";
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

    if (HasOption(command, "json")) {
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

    if (HasOption(command, "json")) {
        std::cout << telemetry_.SerializePerformanceContractJson(targetBudgetMs, limit) << "\n";
        return 0;
    }

    const PerformanceContractSnapshot contract = telemetry_.PerformanceContract(targetBudgetMs, limit);

    std::cout << "Performance contract\n";
    std::cout << "  Samples          : " << contract.sampleCount << "\n";
    std::cout << "  Target budget ms : " << std::fixed << std::setprecision(3) << contract.targetBudgetMs << "\n";
    std::cout << "  p50 total ms     : " << std::fixed << std::setprecision(3) << contract.p50Ms << "\n";
    std::cout << "  p95 total ms     : " << std::fixed << std::setprecision(3) << contract.p95Ms << "\n";
    std::cout << "  max total ms     : " << std::fixed << std::setprecision(3) << contract.maxMs << "\n";
    std::cout << "  jitter ms        : " << std::fixed << std::setprecision(3) << contract.jitterMs << "\n";
    std::cout << "  drift ms         : " << std::fixed << std::setprecision(3) << contract.driftMs << "\n";
    std::cout << "  within budget    : " << (contract.withinBudget ? "true" : "false") << "\n";

    return contract.withinBudget ? 0 : 2;
}

int CliApp::HandleVision(const ParsedCommand& command) {
    const std::size_t limit = ReadSizeOption(command, "limit", 200U, 4096U);

    if (HasOption(command, "json")) {
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

}  // namespace iee
