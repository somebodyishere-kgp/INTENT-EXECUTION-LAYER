#include "AIStateView.h"

#include <Windows.h>

#include <algorithm>
#include <map>
#include <sstream>

namespace iee {
namespace {

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

std::string Narrow(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (requiredBytes <= 1) {
        return "";
    }

    std::string result(static_cast<std::size_t>(requiredBytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), requiredBytes, nullptr, nullptr);
    result.pop_back();
    return result;
}

}  // namespace

AIStateView AIStateViewProjector::Build(const EnvironmentState& state, bool runtimeActive) const {
    AIStateView view;
    view.sequence = state.sequence;
    view.frameId = state.unifiedState.frameId;
    view.graphVersion = state.unifiedState.interactionGraph.version;
    view.graphSignature = state.unifiedState.interactionGraph.signature;
    view.runtimeActive = runtimeActive;
    view.activeWindowTitle = Narrow(state.activeWindowTitle);
    view.activeProcessPath = Narrow(state.activeProcessPath);
    view.nodeCount = state.unifiedState.interactionGraph.nodes.size();
    view.commandCount = state.unifiedState.interactionGraph.commands.size();

    std::map<std::string, std::size_t> actionCounts;

    for (const auto& entry : state.unifiedState.interactionGraph.nodes) {
        const InteractionNode& node = entry.second;
        if (node.hidden || node.offscreen || node.collapsed) {
            ++view.hiddenNodeCount;
            if (view.hiddenNodeIds.size() < 5U) {
                view.hiddenNodeIds.push_back(node.id);
            }
        } else {
            ++view.visibleNodeCount;
        }

        if (node.executionPlan.executable && node.intentBinding.action != IntentAction::Unknown) {
            ++view.actionableNodeCount;
            ++actionCounts[ToString(node.intentBinding.action)];
        }
    }

    for (const auto& entry : actionCounts) {
        view.dominantActions.push_back(AIActionSummary{entry.first, entry.second});
    }

    std::sort(view.dominantActions.begin(), view.dominantActions.end(), [](const AIActionSummary& left, const AIActionSummary& right) {
        if (left.count == right.count) {
            return left.action < right.action;
        }
        return left.count > right.count;
    });

    if (view.dominantActions.size() > 6U) {
        view.dominantActions.resize(6U);
    }

    return view;
}

std::string AIStateViewProjector::SerializeJson(const AIStateView& view) {
    std::ostringstream json;
    json << "{";
    json << "\"sequence\":" << view.sequence << ",";
    json << "\"frame_id\":" << view.frameId << ",";
    json << "\"runtime_active\":" << (view.runtimeActive ? "true" : "false") << ",";
    json << "\"active_window_title\":\"" << EscapeJson(view.activeWindowTitle) << "\",";
    json << "\"active_process_path\":\"" << EscapeJson(view.activeProcessPath) << "\",";
    json << "\"interaction_summary\":{";
    json << "\"graph_version\":" << view.graphVersion << ",";
    json << "\"graph_signature\":" << view.graphSignature << ",";
    json << "\"node_count\":" << view.nodeCount << ",";
    json << "\"visible_node_count\":" << view.visibleNodeCount << ",";
    json << "\"hidden_node_count\":" << view.hiddenNodeCount << ",";
    json << "\"actionable_node_count\":" << view.actionableNodeCount << ",";
    json << "\"command_count\":" << view.commandCount;
    json << "},";

    json << "\"dominant_actions\":[";
    for (std::size_t index = 0; index < view.dominantActions.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "{";
        json << "\"action\":\"" << EscapeJson(view.dominantActions[index].action) << "\",";
        json << "\"count\":" << view.dominantActions[index].count;
        json << "}";
    }
    json << "],";

    json << "\"hidden_nodes\":[";
    for (std::size_t index = 0; index < view.hiddenNodeIds.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "\"" << EscapeJson(view.hiddenNodeIds[index]) << "\"";
    }
    json << "]";

    json << "}";
    return json.str();
}

}  // namespace iee
