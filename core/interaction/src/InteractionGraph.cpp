#include "InteractionGraph.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace iee {
namespace {

std::wstring Wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return L"";
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), required);
    wide.pop_back();
    return wide;
}

std::string Narrow(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return "";
    }

    std::string narrow(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, narrow.data(), required, nullptr, nullptr);
    narrow.pop_back();
    return narrow;
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

std::string ToAsciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
}

std::uint64_t StableHash(std::string_view input) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : input) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return hash == 0 ? 1ULL : hash;
}

std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

std::uint64_t HashBool(std::uint64_t seed, bool value) {
    return HashCombine(seed, value ? 0x9A17ULL : 0x83ULL);
}

std::string HexFromHash(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string NodeTypeFromControl(UiControlType type) {
    switch (type) {
    case UiControlType::Button:
        return "button";
    case UiControlType::TextBox:
        return "text_box";
    case UiControlType::Menu:
        return "menu";
    case UiControlType::MenuItem:
        return "menu_item";
    case UiControlType::ComboBox:
        return "combo_box";
    case UiControlType::ListItem:
        return "list_item";
    case UiControlType::Document:
        return "document";
    case UiControlType::Window:
        return "window";
    case UiControlType::Unknown:
    default:
        return "unknown";
    }
}

std::string SelectNodeLabel(const UiElement& element) {
    if (!element.name.empty()) {
        return Narrow(element.name);
    }
    if (!element.automationId.empty()) {
        return Narrow(element.automationId);
    }
    if (!element.className.empty()) {
        return Narrow(element.className);
    }
    return "";
}

std::string SelectAutomationId(const UiElement& element) {
    if (!element.automationId.empty()) {
        return Narrow(element.automationId);
    }
    if (!element.id.empty()) {
        return element.id;
    }
    return "";
}

std::string SelectShortcut(const UiElement& element) {
    if (!element.acceleratorKey.empty()) {
        return Narrow(element.acceleratorKey);
    }
    if (!element.accessKey.empty()) {
        return Narrow(element.accessKey);
    }

    const std::wstring marker = L"Ctrl+";
    if (!element.name.empty() && element.name.find(marker) != std::wstring::npos) {
        return Narrow(element.name.substr(element.name.find(marker)));
    }

    return "";
}

bool IsInteractiveNodeType(std::string_view type) {
    return type == "button" ||
        type == "text_box" ||
        type == "menu" ||
        type == "menu_item" ||
        type == "combo_box" ||
        type == "list_item" ||
        type == "document" ||
        type == "command";
}

IntentAction ActionForNodeType(std::string_view type) {
    if (type == "text_box" || type == "document") {
        return IntentAction::SetValue;
    }

    if (type == "menu" || type == "menu_item" || type == "combo_box" || type == "list_item") {
        return IntentAction::Select;
    }

    return IntentAction::Activate;
}

std::string BuildPathSegment(const UiElement& element) {
    const std::string role = NodeTypeFromControl(element.controlType);
    std::string label = ToAsciiLower(SelectNodeLabel(element));
    if (label.empty()) {
        label = ToAsciiLower(SelectAutomationId(element));
    }
    if (label.empty()) {
        label = "anonymous";
    }

    return role + ":" + label;
}

std::string ResolveUiPath(
    const UiElement& element,
    const std::unordered_map<std::string, const UiElement*>& elementsById,
    std::unordered_map<std::string, std::string>* cache,
    std::unordered_set<std::string>* visiting) {
    if (cache == nullptr || visiting == nullptr) {
        return "root/" + BuildPathSegment(element);
    }

    if (element.id.empty()) {
        return "root/" + BuildPathSegment(element);
    }

    const auto cached = cache->find(element.id);
    if (cached != cache->end()) {
        return cached->second;
    }

    if (!visiting->insert(element.id).second) {
        return "cycle/" + BuildPathSegment(element);
    }

    std::string parentPath = "root";
    if (!element.parentId.empty()) {
        const auto parent = elementsById.find(element.parentId);
        if (parent != elementsById.end() && parent->second != nullptr) {
            parentPath = ResolveUiPath(*parent->second, elementsById, cache, visiting);
        }
    }

    visiting->erase(element.id);

    const std::string path = parentPath + "/" + BuildPathSegment(element);
    (*cache)[element.id] = path;
    return path;
}

NodeId BuildCommandNodeId(
    const std::string& parentNodeId,
    const std::string& shortcut,
    const std::string& name,
    std::size_t ordinal) {
    const std::string key =
        "command|" + parentNodeId + "|" + ToAsciiLower(shortcut) + "|" + ToAsciiLower(name) + "|" + std::to_string(ordinal);

    NodeId nodeId;
    nodeId.signature = StableHash(key);
    nodeId.stableId = "uig-cmd-" + HexFromHash(nodeId.signature);
    return nodeId;
}

std::string BuildStepId(
    const std::string& nodeId,
    const std::string& action,
    const std::string& targetId,
    std::size_t index) {
    const std::string key = nodeId + "|" + action + "|" + targetId + "|" + std::to_string(index);
    return "step-" + HexFromHash(StableHash(key));
}

PlanStep MakeStep(
    const std::string& id,
    const std::string& action,
    const std::string& targetId,
    const std::string& argument,
    bool requiresVisible) {
    PlanStep step;
    step.id = id;
    step.action = action;
    step.targetId = targetId;
    step.argument = argument;
    step.requiresVisible = requiresVisible;
    return step;
}

RevealStrategy BuildRevealStrategy(const InteractionNode& node) {
    RevealStrategy reveal;
    reveal.required = node.hidden || node.offscreen || node.collapsed;

    if (!reveal.required) {
        reveal.guaranteed = true;
        reveal.reason = "already_visible";
        return reveal;
    }

    std::size_t stepIndex = 0;
    if (node.collapsed && !node.parentId.empty()) {
        reveal.steps.push_back(MakeStep(
            BuildStepId(node.id, "expand_parent", node.parentId, stepIndex++),
            "expand_parent",
            node.parentId,
            "",
            false));
    }

    if (node.hidden && !node.parentId.empty()) {
        reveal.steps.push_back(MakeStep(
            BuildStepId(node.id, "focus_parent", node.parentId, stepIndex++),
            "focus_parent",
            node.parentId,
            "",
            false));
    }

    if (node.offscreen) {
        reveal.steps.push_back(MakeStep(
            BuildStepId(node.id, "scroll_into_view", node.id, stepIndex++),
            "scroll_into_view",
            node.id,
            "",
            false));
    }

    if (reveal.steps.empty()) {
        reveal.steps.push_back(MakeStep(
            BuildStepId(node.id, "probe_visibility", node.id, stepIndex),
            "probe_visibility",
            node.id,
            "",
            false));
    }

    reveal.guaranteed = !reveal.steps.empty();
    reveal.reason = reveal.guaranteed ? "reveal_required" : "reveal_unavailable";
    return reveal;
}

ExecutionPlan BuildExecutionPlan(const InteractionNode& node, const RevealStrategy& reveal) {
    ExecutionPlan plan;
    plan.id = "plan-" + node.id;

    for (const PlanStep& step : reveal.steps) {
        plan.steps.push_back(step);
    }

    const bool interactive = IsInteractiveNodeType(node.type);
    if (interactive) {
        const IntentAction action = ActionForNodeType(node.type);
        plan.steps.push_back(MakeStep(
            BuildStepId(node.id, ToString(action), node.id, plan.steps.size()),
            ToString(action),
            node.id,
            node.shortcut,
            true));
    } else {
        plan.steps.push_back(MakeStep(
            BuildStepId(node.id, "observe", node.id, plan.steps.size()),
            "observe",
            node.id,
            "",
            false));
    }

    if (!interactive) {
        plan.executable = false;
        plan.reason = "non_interactive";
        return plan;
    }

    if (!node.enabled) {
        plan.executable = false;
        plan.reason = "node_disabled";
        return plan;
    }

    if (reveal.required && !reveal.guaranteed) {
        plan.executable = false;
        plan.reason = "reveal_unavailable";
        return plan;
    }

    plan.executable = true;
    plan.reason = reveal.required ? "ready_with_reveal" : "ready";
    return plan;
}

NodeIntentBinding BuildIntentBinding(const InteractionNode& node, const ExecutionPlan& plan, const RevealStrategy& reveal) {
    NodeIntentBinding binding;
    binding.nodeId = node.id;
    binding.action = IsInteractiveNodeType(node.type) ? ActionForNodeType(node.type) : IntentAction::Unknown;
    binding.plan = plan;
    binding.reveal = reveal;
    return binding;
}

void PopulateNodeContracts(InteractionNode* node, std::uint64_t sequence) {
    if (node == nullptr) {
        return;
    }

    node->descriptor.id = node->id;
    node->descriptor.nodeId = node->nodeId;
    node->descriptor.label = node->label;
    node->descriptor.type = node->type;
    node->descriptor.parentId = node->parentId;
    node->descriptor.source = node->source;
    node->descriptor.uiElementId = node->uiElementId;
    node->descriptor.automationId = node->uiElementId;
    node->descriptor.shortcut = node->shortcut;
    node->descriptor.accessKey = "";
    node->descriptor.bounds = node->bounds;
    node->descriptor.children = node->children;
    node->descriptor.commandNode = node->type == "command";

    node->state.visible = node->visible;
    node->state.enabled = node->enabled;
    node->state.offscreen = node->offscreen;
    node->state.collapsed = node->collapsed;
    node->state.hidden = node->hidden;
    node->state.sequence = sequence;
    node->state.confidence = node->confidence;

    node->revealStrategy = BuildRevealStrategy(*node);
    node->executionPlan = BuildExecutionPlan(*node, node->revealStrategy);
    node->intentBinding = BuildIntentBinding(*node, node->executionPlan, node->revealStrategy);
}

std::uint64_t StableNodeDigest(const InteractionNode& node) {
    std::uint64_t digest = StableHash(node.id);
    digest = HashCombine(digest, node.nodeId.signature);
    digest = HashCombine(digest, StableHash(node.descriptor.type));
    digest = HashCombine(digest, StableHash(node.descriptor.label));
    digest = HashCombine(digest, StableHash(node.descriptor.parentId));
    digest = HashCombine(digest, StableHash(node.descriptor.source));
    digest = HashCombine(digest, StableHash(node.descriptor.uiElementId));
    digest = HashCombine(digest, StableHash(node.descriptor.shortcut));
    digest = HashCombine(digest, static_cast<std::uint64_t>(static_cast<std::int64_t>(node.bounds.left)));
    digest = HashCombine(digest, static_cast<std::uint64_t>(static_cast<std::int64_t>(node.bounds.top)));
    digest = HashCombine(digest, static_cast<std::uint64_t>(static_cast<std::int64_t>(node.bounds.right)));
    digest = HashCombine(digest, static_cast<std::uint64_t>(static_cast<std::int64_t>(node.bounds.bottom)));

    digest = HashBool(digest, node.state.visible);
    digest = HashBool(digest, node.state.enabled);
    digest = HashBool(digest, node.state.offscreen);
    digest = HashBool(digest, node.state.collapsed);
    digest = HashBool(digest, node.state.hidden);

    digest = HashCombine(digest, StableHash(node.executionPlan.id));
    digest = HashBool(digest, node.executionPlan.executable);
    digest = HashCombine(digest, StableHash(node.executionPlan.reason));

    for (const PlanStep& step : node.executionPlan.steps) {
        digest = HashCombine(digest, StableHash(step.id));
        digest = HashCombine(digest, StableHash(step.action));
        digest = HashCombine(digest, StableHash(step.targetId));
        digest = HashCombine(digest, StableHash(step.argument));
        digest = HashBool(digest, step.requiresVisible);
    }

    for (const std::string& child : node.children) {
        digest = HashCombine(digest, StableHash(child));
    }

    return digest == 0 ? 1ULL : digest;
}

std::uint64_t ComputeGraphSignature(
    const std::unordered_map<std::string, InteractionNode>& nodes,
    const std::vector<InteractionEdge>& edges,
    const std::vector<CommandNode>& commands) {
    std::vector<std::string> nodeIds;
    nodeIds.reserve(nodes.size());
    for (const auto& entry : nodes) {
        nodeIds.push_back(entry.first);
    }
    std::sort(nodeIds.begin(), nodeIds.end());

    std::uint64_t signature = 1469598103934665603ULL;
    for (const std::string& nodeId : nodeIds) {
        const auto it = nodes.find(nodeId);
        if (it == nodes.end()) {
            continue;
        }
        signature = HashCombine(signature, StableNodeDigest(it->second));
    }

    std::vector<InteractionEdge> orderedEdges = edges;
    std::sort(orderedEdges.begin(), orderedEdges.end(), [](const InteractionEdge& left, const InteractionEdge& right) {
        return left.from == right.from ? left.to < right.to : left.from < right.from;
    });

    for (const InteractionEdge& edge : orderedEdges) {
        signature = HashCombine(signature, StableHash(edge.from));
        signature = HashCombine(signature, StableHash(edge.to));
    }

    std::vector<CommandNode> orderedCommands = commands;
    std::sort(orderedCommands.begin(), orderedCommands.end(), [](const CommandNode& left, const CommandNode& right) {
        return left.id < right.id;
    });

    for (const CommandNode& command : orderedCommands) {
        signature = HashCombine(signature, StableHash(command.id));
        signature = HashCombine(signature, StableHash(command.name));
        signature = HashCombine(signature, StableHash(command.shortcut));
        signature = HashCombine(signature, StableHash(command.source));
    }

    return signature == 0 ? 1ULL : signature;
}

std::string SerializeRectJson(const RECT& rect) {
    std::ostringstream json;
    json << "{";
    json << "\"left\":" << rect.left << ",";
    json << "\"top\":" << rect.top << ",";
    json << "\"right\":" << rect.right << ",";
    json << "\"bottom\":" << rect.bottom;
    json << "}";
    return json.str();
}

std::string SerializeStringVectorJson(const std::vector<std::string>& values) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "\"" << EscapeJson(values[index]) << "\"";
    }
    json << "]";
    return json.str();
}

std::string SerializePlanStepsJson(const std::vector<PlanStep>& steps) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < steps.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        json << "{";
        json << "\"id\":\"" << EscapeJson(steps[index].id) << "\",";
        json << "\"action\":\"" << EscapeJson(steps[index].action) << "\",";
        json << "\"target_id\":\"" << EscapeJson(steps[index].targetId) << "\",";
        json << "\"argument\":\"" << EscapeJson(steps[index].argument) << "\",";
        json << "\"requires_visible\":" << (steps[index].requiresVisible ? "true" : "false");
        json << "}";
    }
    json << "]";
    return json.str();
}

}  // namespace

InteractionGraph InteractionGraphBuilder::Build(const std::vector<UiElement>& uiElements, std::uint64_t sequence) {
    InteractionGraph graph;
    graph.sequence = sequence;
    graph.version = sequence;

    std::unordered_map<std::string, const UiElement*> elementsById;
    elementsById.reserve(uiElements.size());
    for (const UiElement& element : uiElements) {
        if (!element.id.empty()) {
            elementsById[element.id] = &element;
        }
    }

    std::unordered_map<std::string, std::string> pathCache;
    std::unordered_set<std::string> visiting;
    std::unordered_map<std::string, std::string> nodeByUiElementId;
    std::unordered_set<std::string> occupiedNodeIds;

    nodeByUiElementId.reserve(uiElements.size());
    occupiedNodeIds.reserve(uiElements.size() * 2U);

    for (std::size_t index = 0; index < uiElements.size(); ++index) {
        const UiElement& element = uiElements[index];

        const std::string uiPath = ResolveUiPath(element, elementsById, &pathCache, &visiting);

        NodeId identity = BuildNodeId(element, uiPath, index);
        std::size_t collision = 0;
        while (occupiedNodeIds.find(identity.stableId) != occupiedNodeIds.end()) {
            ++collision;
            identity = BuildNodeId(element, uiPath + "|collision=" + std::to_string(collision), index + collision);
        }
        occupiedNodeIds.insert(identity.stableId);

        InteractionNode node;
        node.id = identity.stableId;
        node.nodeId = identity;
        node.label = SelectNodeLabel(element);
        node.type = NodeTypeFromControl(element.controlType);
        node.visible = element.isVisible && !element.isOffscreen;
        node.enabled = element.isEnabled;
        node.offscreen = element.isOffscreen;
        node.collapsed = element.isCollapsed;
        node.hidden = element.isHidden || !node.visible || !node.enabled || node.collapsed;
        node.parentId = element.parentId;
        node.source = "UIA";
        node.uiElementId = element.id.empty() ? SelectAutomationId(element) : element.id;
        node.shortcut = SelectShortcut(element);
        node.bounds = element.bounds;
        node.confidence = node.visible ? 0.95 : 0.72;

        if (!element.id.empty()) {
            nodeByUiElementId[element.id] = node.id;
        }

        graph.nodes[node.id] = std::move(node);
    }

    for (auto& entry : graph.nodes) {
        entry.second.children.clear();
    }

    for (const UiElement& element : uiElements) {
        if (element.id.empty()) {
            continue;
        }

        const auto nodeIt = nodeByUiElementId.find(element.id);
        if (nodeIt == nodeByUiElementId.end()) {
            continue;
        }

        const std::string& nodeId = nodeIt->second;
        InteractionNode& node = graph.nodes[nodeId];

        if (!element.parentId.empty()) {
            const auto parentIt = nodeByUiElementId.find(element.parentId);
            if (parentIt != nodeByUiElementId.end()) {
                node.parentId = parentIt->second;
                InteractionNode& parent = graph.nodes[parentIt->second];
                parent.children.push_back(nodeId);
                graph.edges.push_back({parent.id, nodeId});
            } else {
                node.parentId.clear();
            }
        } else {
            node.parentId.clear();
        }
    }

    std::size_t commandOrdinal = 0;
    for (const UiElement& element : uiElements) {
        if (element.id.empty()) {
            continue;
        }

        const auto parentIt = nodeByUiElementId.find(element.id);
        if (parentIt == nodeByUiElementId.end()) {
            continue;
        }

        const std::string shortcut = SelectShortcut(element);
        if (shortcut.empty()) {
            continue;
        }

        const std::string parentNodeId = parentIt->second;
        const std::string name = SelectNodeLabel(element).empty() ? parentNodeId : SelectNodeLabel(element);

        NodeId commandIdentity = BuildCommandNodeId(parentNodeId, shortcut, name, commandOrdinal++);
        std::size_t collision = 0;
        while (occupiedNodeIds.find(commandIdentity.stableId) != occupiedNodeIds.end()) {
            ++collision;
            commandIdentity = BuildCommandNodeId(parentNodeId, shortcut, name, commandOrdinal + collision);
        }
        occupiedNodeIds.insert(commandIdentity.stableId);

        CommandNode command;
        command.id = commandIdentity.stableId;
        command.name = name;
        command.shortcut = shortcut;
        command.source = "command";
        graph.commands.push_back(command);

        InteractionNode commandNode;
        commandNode.id = command.id;
        commandNode.nodeId = commandIdentity;
        commandNode.label = command.name;
        commandNode.type = "command";
        commandNode.visible = true;
        commandNode.enabled = true;
        commandNode.offscreen = false;
        commandNode.collapsed = false;
        commandNode.hidden = false;
        commandNode.parentId = parentNodeId;
        commandNode.source = "command";
        commandNode.uiElementId = parentNodeId;
        commandNode.shortcut = command.shortcut;
        commandNode.bounds = element.bounds;
        commandNode.confidence = 0.80;

        graph.nodes[commandNode.id] = commandNode;
        graph.nodes[parentNodeId].children.push_back(commandNode.id);
        graph.edges.push_back({parentNodeId, commandNode.id});
    }

    for (auto& entry : graph.nodes) {
        std::vector<std::string>& children = entry.second.children;
        std::sort(children.begin(), children.end());
        children.erase(std::unique(children.begin(), children.end()), children.end());

        PopulateNodeContracts(&entry.second, graph.sequence);
    }

    graph.signature = ComputeGraphSignature(graph.nodes, graph.edges, graph.commands);
    graph.valid = !graph.nodes.empty();
    return graph;
}

std::optional<InteractionNode> InteractionGraphBuilder::FindNode(const InteractionGraph& graph, const std::string& nodeId) {
    const auto it = graph.nodes.find(nodeId);
    if (it == graph.nodes.end()) {
        return std::nullopt;
    }
    return it->second;
}

Intent InteractionGraphBuilder::GenerateIntent(const InteractionNode& node) {
    Intent intent;

    IntentAction action = node.intentBinding.action;
    if (action == IntentAction::Unknown) {
        action = ActionForNodeType(node.type);
    }

    intent.action = action;
    intent.name = ToString(intent.action);
    intent.target.type = TargetType::UiElement;
    intent.target.nodeId = node.id;
    intent.target.label = Wide(node.label);
    intent.target.focused = false;
    intent.target.hierarchyDepth = 0;
    intent.target.screenCenter = {
        (node.bounds.left + node.bounds.right) / 2,
        (node.bounds.top + node.bounds.bottom) / 2};

    if (!node.shortcut.empty()) {
        intent.params.values["shortcut"] = Wide(node.shortcut);
    }

    if (!node.enabled) {
        intent.params.values["requires_enable"] = L"true";
    }

    if (!node.visible) {
        intent.params.values["target_visibility"] = L"hidden";
    }

    if (!node.executionPlan.id.empty()) {
        intent.params.values["plan_id"] = Wide(node.executionPlan.id);
        intent.params.values["plan_step_count"] = std::to_wstring(node.executionPlan.steps.size());
        intent.params.values["plan_executable"] = node.executionPlan.executable ? L"true" : L"false";
    }

    if (node.revealStrategy.required) {
        intent.params.values["reveal_required"] = L"true";
        intent.params.values["reveal_step_count"] = std::to_wstring(node.revealStrategy.steps.size());
    }

    if (!node.executionPlan.steps.empty()) {
        intent.params.values["next_step"] = Wide(node.executionPlan.steps.front().action);
    }

    intent.constraints.requiresVerification = true;
    intent.constraints.allowFallback = true;
    intent.confidence = static_cast<float>(std::clamp(node.state.confidence > 0.0 ? node.state.confidence : node.confidence, 0.0, 1.0));
    intent.source = node.source.empty() ? "UIA" : node.source;
    intent.id = "uig-intent-" + node.id + "-" + intent.name;
    return intent;
}

std::vector<Intent> InteractionGraphBuilder::GenerateIntents(
    const InteractionGraph& graph,
    bool includeHidden,
    std::size_t maxIntents) {
    std::vector<std::string> nodeIds;
    nodeIds.reserve(graph.nodes.size());
    for (const auto& entry : graph.nodes) {
        nodeIds.push_back(entry.first);
    }
    std::sort(nodeIds.begin(), nodeIds.end());

    std::vector<Intent> intents;
    intents.reserve(std::min<std::size_t>(nodeIds.size(), maxIntents));

    for (const std::string& nodeId : nodeIds) {
        const auto it = graph.nodes.find(nodeId);
        if (it == graph.nodes.end()) {
            continue;
        }

        const InteractionNode& node = it->second;
        if (!includeHidden && node.hidden) {
            continue;
        }

        if (!IsInteractiveNodeType(node.type)) {
            continue;
        }

        intents.push_back(GenerateIntent(node));
        if (intents.size() >= maxIntents) {
            break;
        }
    }

    return intents;
}

std::optional<ExecutionPlan> InteractionGraphBuilder::GetExecutionPlan(
    const InteractionGraph& graph,
    const std::string& nodeId) {
    const auto it = graph.nodes.find(nodeId);
    if (it == graph.nodes.end()) {
        return std::nullopt;
    }
    return it->second.executionPlan;
}

std::optional<RevealStrategy> InteractionGraphBuilder::GetRevealStrategy(
    const InteractionGraph& graph,
    const std::string& nodeId) {
    const auto it = graph.nodes.find(nodeId);
    if (it == graph.nodes.end()) {
        return std::nullopt;
    }
    return it->second.revealStrategy;
}

std::optional<NodeIntentBinding> InteractionGraphBuilder::GetIntentBinding(
    const InteractionGraph& graph,
    const std::string& nodeId) {
    const auto it = graph.nodes.find(nodeId);
    if (it == graph.nodes.end()) {
        return std::nullopt;
    }
    return it->second.intentBinding;
}

GraphDelta InteractionGraphBuilder::ComputeDelta(const InteractionGraph& base, const InteractionGraph& current) {
    GraphDelta delta;
    delta.fromVersion = base.version;
    delta.toVersion = current.version;

    std::unordered_map<std::string, std::uint64_t> baseDigest;
    baseDigest.reserve(base.nodes.size());
    for (const auto& entry : base.nodes) {
        baseDigest[entry.first] = StableNodeDigest(entry.second);
    }

    std::unordered_map<std::string, std::uint64_t> currentDigest;
    currentDigest.reserve(current.nodes.size());
    for (const auto& entry : current.nodes) {
        currentDigest[entry.first] = StableNodeDigest(entry.second);
    }

    for (const auto& entry : currentDigest) {
        const auto baseIt = baseDigest.find(entry.first);
        if (baseIt == baseDigest.end()) {
            delta.addedNodes.push_back(entry.first);
            continue;
        }

        if (baseIt->second != entry.second) {
            delta.updatedNodes.push_back(entry.first);
        }
    }

    for (const auto& entry : baseDigest) {
        if (currentDigest.find(entry.first) == currentDigest.end()) {
            delta.removedNodes.push_back(entry.first);
        }
    }

    std::sort(delta.addedNodes.begin(), delta.addedNodes.end());
    std::sort(delta.updatedNodes.begin(), delta.updatedNodes.end());
    std::sort(delta.removedNodes.begin(), delta.removedNodes.end());

    delta.changed = !delta.addedNodes.empty() || !delta.updatedNodes.empty() || !delta.removedNodes.empty();
    return delta;
}

std::string InteractionGraphBuilder::SerializeExecutionPlanJson(const ExecutionPlan& plan) {
    std::ostringstream json;
    json << "{";
    json << "\"id\":\"" << EscapeJson(plan.id) << "\",";
    json << "\"executable\":" << (plan.executable ? "true" : "false") << ",";
    json << "\"reason\":\"" << EscapeJson(plan.reason) << "\",";
    json << "\"steps\":" << SerializePlanStepsJson(plan.steps);
    json << "}";
    return json.str();
}

std::string InteractionGraphBuilder::SerializeRevealStrategyJson(const RevealStrategy& reveal) {
    std::ostringstream json;
    json << "{";
    json << "\"required\":" << (reveal.required ? "true" : "false") << ",";
    json << "\"guaranteed\":" << (reveal.guaranteed ? "true" : "false") << ",";
    json << "\"reason\":\"" << EscapeJson(reveal.reason) << "\",";
    json << "\"steps\":" << SerializePlanStepsJson(reveal.steps);
    json << "}";
    return json.str();
}

std::string InteractionGraphBuilder::SerializeIntentBindingJson(const NodeIntentBinding& binding) {
    std::ostringstream json;
    json << "{";
    json << "\"node_id\":\"" << EscapeJson(binding.nodeId) << "\",";
    json << "\"action\":\"" << EscapeJson(ToString(binding.action)) << "\",";
    json << "\"plan\":" << SerializeExecutionPlanJson(binding.plan) << ",";
    json << "\"reveal\":" << SerializeRevealStrategyJson(binding.reveal);
    json << "}";
    return json.str();
}

std::string InteractionGraphBuilder::SerializeDeltaJson(const GraphDelta& delta) {
    std::ostringstream json;
    json << "{";
    json << "\"from_version\":" << delta.fromVersion << ",";
    json << "\"to_version\":" << delta.toVersion << ",";
    json << "\"changed\":" << (delta.changed ? "true" : "false") << ",";
    json << "\"reset_required\":" << (delta.resetRequired ? "true" : "false") << ",";
    json << "\"added_nodes\":" << SerializeStringVectorJson(delta.addedNodes) << ",";
    json << "\"updated_nodes\":" << SerializeStringVectorJson(delta.updatedNodes) << ",";
    json << "\"removed_nodes\":" << SerializeStringVectorJson(delta.removedNodes);
    json << "}";
    return json.str();
}

std::string InteractionGraphBuilder::SerializeNodeJson(const InteractionNode& node) {
    std::ostringstream json;
    json << "{";
    json << "\"id\":\"" << EscapeJson(node.id) << "\",";
    json << "\"stable_id\":\"" << EscapeJson(node.nodeId.stableId) << "\",";
    json << "\"identity_signature\":" << node.nodeId.signature << ",";
    json << "\"label\":\"" << EscapeJson(node.label) << "\",";
    json << "\"type\":\"" << EscapeJson(node.type) << "\",";
    json << "\"visible\":" << (node.visible ? "true" : "false") << ",";
    json << "\"enabled\":" << (node.enabled ? "true" : "false") << ",";
    json << "\"offscreen\":" << (node.offscreen ? "true" : "false") << ",";
    json << "\"collapsed\":" << (node.collapsed ? "true" : "false") << ",";
    json << "\"hidden\":" << (node.hidden ? "true" : "false") << ",";
    json << "\"parent_id\":\"" << EscapeJson(node.parentId) << "\",";
    json << "\"source\":\"" << EscapeJson(node.source) << "\",";
    json << "\"ui_element_id\":\"" << EscapeJson(node.uiElementId) << "\",";
    json << "\"shortcut\":\"" << EscapeJson(node.shortcut) << "\",";
    json << "\"confidence\":" << node.confidence << ",";
    json << "\"bounds\":" << SerializeRectJson(node.bounds) << ",";
    json << "\"children\":" << SerializeStringVectorJson(node.children) << ",";

    json << "\"descriptor\":{";
    json << "\"id\":\"" << EscapeJson(node.descriptor.id) << "\",";
    json << "\"node_id\":{\"stable_id\":\"" << EscapeJson(node.descriptor.nodeId.stableId)
         << "\",\"signature\":" << node.descriptor.nodeId.signature << "},";
    json << "\"label\":\"" << EscapeJson(node.descriptor.label) << "\",";
    json << "\"type\":\"" << EscapeJson(node.descriptor.type) << "\",";
    json << "\"parent_id\":\"" << EscapeJson(node.descriptor.parentId) << "\",";
    json << "\"source\":\"" << EscapeJson(node.descriptor.source) << "\",";
    json << "\"ui_element_id\":\"" << EscapeJson(node.descriptor.uiElementId) << "\",";
    json << "\"automation_id\":\"" << EscapeJson(node.descriptor.automationId) << "\",";
    json << "\"shortcut\":\"" << EscapeJson(node.descriptor.shortcut) << "\",";
    json << "\"access_key\":\"" << EscapeJson(node.descriptor.accessKey) << "\",";
    json << "\"bounds\":" << SerializeRectJson(node.descriptor.bounds) << ",";
    json << "\"children\":" << SerializeStringVectorJson(node.descriptor.children) << ",";
    json << "\"command_node\":" << (node.descriptor.commandNode ? "true" : "false");
    json << "},";

    json << "\"state\":{";
    json << "\"visible\":" << (node.state.visible ? "true" : "false") << ",";
    json << "\"enabled\":" << (node.state.enabled ? "true" : "false") << ",";
    json << "\"offscreen\":" << (node.state.offscreen ? "true" : "false") << ",";
    json << "\"collapsed\":" << (node.state.collapsed ? "true" : "false") << ",";
    json << "\"hidden\":" << (node.state.hidden ? "true" : "false") << ",";
    json << "\"sequence\":" << node.state.sequence << ",";
    json << "\"confidence\":" << node.state.confidence;
    json << "},";

    json << "\"execution_plan\":" << SerializeExecutionPlanJson(node.executionPlan) << ",";
    json << "\"reveal_strategy\":" << SerializeRevealStrategyJson(node.revealStrategy) << ",";
    json << "\"intent_binding\":" << SerializeIntentBindingJson(node.intentBinding);
    json << "}";
    return json.str();
}

std::string InteractionGraphBuilder::SerializeGraphJson(const InteractionGraph& graph) {
    std::vector<std::string> nodeIds;
    nodeIds.reserve(graph.nodes.size());
    for (const auto& entry : graph.nodes) {
        nodeIds.push_back(entry.first);
    }
    std::sort(nodeIds.begin(), nodeIds.end());

    std::vector<InteractionEdge> orderedEdges = graph.edges;
    std::sort(orderedEdges.begin(), orderedEdges.end(), [](const InteractionEdge& left, const InteractionEdge& right) {
        return left.from == right.from ? left.to < right.to : left.from < right.from;
    });

    std::vector<CommandNode> orderedCommands = graph.commands;
    std::sort(orderedCommands.begin(), orderedCommands.end(), [](const CommandNode& left, const CommandNode& right) {
        return left.id < right.id;
    });

    std::ostringstream json;
    json << "{";
    json << "\"sequence\":" << graph.sequence << ",";
    json << "\"version\":" << graph.version << ",";
    json << "\"signature\":" << graph.signature << ",";
    json << "\"valid\":" << (graph.valid ? "true" : "false") << ",";
    json << "\"nodes\":[";

    for (std::size_t index = 0; index < nodeIds.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const auto it = graph.nodes.find(nodeIds[index]);
        if (it != graph.nodes.end()) {
            json << SerializeNodeJson(it->second);
        }
    }

    json << "],";
    json << "\"edges\":[";

    for (std::size_t index = 0; index < orderedEdges.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        json << "{";
        json << "\"from\":\"" << EscapeJson(orderedEdges[index].from) << "\",";
        json << "\"to\":\"" << EscapeJson(orderedEdges[index].to) << "\"";
        json << "}";
    }

    json << "],";
    json << "\"commands\":[";

    for (std::size_t index = 0; index < orderedCommands.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        json << "{";
        json << "\"id\":\"" << EscapeJson(orderedCommands[index].id) << "\",";
        json << "\"name\":\"" << EscapeJson(orderedCommands[index].name) << "\",";
        json << "\"shortcut\":\"" << EscapeJson(orderedCommands[index].shortcut) << "\",";
        json << "\"source\":\"" << EscapeJson(orderedCommands[index].source) << "\"";
        json << "}";
    }

    json << "]";
    json << "}";
    return json.str();
}

NodeId InteractionGraphBuilder::BuildNodeId(const UiElement& element, const std::string& uiPath, std::size_t fallbackIndex) {
    const std::string role = NodeTypeFromControl(element.controlType);
    const std::string label = ToAsciiLower(SelectNodeLabel(element));
    std::string automationId = ToAsciiLower(SelectAutomationId(element));
    if (automationId.empty()) {
        automationId = "fallback_" + std::to_string(fallbackIndex);
    }

    const std::string identityKey =
        uiPath + "|role=" + role + "|label=" + label + "|automation=" + automationId;

    NodeId nodeId;
    nodeId.signature = StableHash(identityKey);
    nodeId.stableId = "uig-node-" + HexFromHash(nodeId.signature);
    return nodeId;
}

}  // namespace iee
