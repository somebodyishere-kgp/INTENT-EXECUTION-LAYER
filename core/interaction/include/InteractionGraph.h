#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Intent.h"
#include "UiElement.h"

namespace iee {

struct NodeId {
    std::string stableId;
    std::uint64_t signature{0};
};

struct InteractionDescriptor {
    std::string id;
    NodeId nodeId;
    std::string label;
    std::string type;
    std::string parentId;
    std::string source;
    std::string uiElementId;
    std::string automationId;
    std::string shortcut;
    std::string accessKey;
    RECT bounds{0, 0, 0, 0};
    std::vector<std::string> children;
    bool commandNode{false};
};

struct InteractionState {
    bool visible{false};
    bool enabled{false};
    bool offscreen{false};
    bool collapsed{false};
    bool hidden{false};
    std::uint64_t sequence{0};
    double confidence{0.0};
};

struct PlanStep {
    std::string id;
    std::string action;
    std::string targetId;
    std::string argument;
    bool requiresVisible{true};
};

struct ExecutionPlan {
    std::string id;
    std::vector<PlanStep> steps;
    bool executable{false};
    std::string reason;
};

struct RevealStrategy {
    bool required{false};
    bool guaranteed{false};
    std::string reason;
    std::vector<PlanStep> steps;
};

struct NodeIntentBinding {
    std::string nodeId;
    IntentAction action{IntentAction::Unknown};
    ExecutionPlan plan;
    RevealStrategy reveal;
};

struct GraphDelta {
    std::uint64_t fromVersion{0};
    std::uint64_t toVersion{0};
    bool changed{false};
    bool resetRequired{false};
    std::vector<std::string> addedNodes;
    std::vector<std::string> updatedNodes;
    std::vector<std::string> removedNodes;
};

struct CommandNode {
    std::string id;
    std::string name;
    std::string shortcut;
    std::string source{"command"};
};

struct InteractionNode {
    // Legacy fields retained for backward compatibility.
    std::string id;
    std::string label;
    std::string type;
    bool visible{false};
    bool enabled{false};
    bool offscreen{false};
    bool collapsed{false};
    bool hidden{false};
    std::vector<std::string> children;
    std::string parentId;
    std::string source{"UIA"};
    std::string uiElementId;
    std::string shortcut;
    RECT bounds{0, 0, 0, 0};
    double confidence{0.0};

    // v1.6 contracts.
    NodeId nodeId;
    InteractionDescriptor descriptor;
    InteractionState state;
    ExecutionPlan executionPlan;
    RevealStrategy revealStrategy;
    NodeIntentBinding intentBinding;
};

struct InteractionEdge {
    std::string from;
    std::string to;
};

struct InteractionGraph {
    std::uint64_t sequence{0};
    std::uint64_t version{0};
    std::uint64_t signature{0};
    std::unordered_map<std::string, InteractionNode> nodes;
    std::vector<InteractionEdge> edges;
    std::vector<CommandNode> commands;
    bool valid{false};
};

class InteractionGraphBuilder {
public:
    static InteractionGraph Build(
        const std::vector<UiElement>& uiElements,
        std::uint64_t sequence = 0);

    static std::optional<InteractionNode> FindNode(
        const InteractionGraph& graph,
        const std::string& nodeId);

    static Intent GenerateIntent(const InteractionNode& node);

    static std::vector<Intent> GenerateIntents(
        const InteractionGraph& graph,
        bool includeHidden,
        std::size_t maxIntents = 4096U);

    static std::optional<ExecutionPlan> GetExecutionPlan(
        const InteractionGraph& graph,
        const std::string& nodeId);

    static std::optional<RevealStrategy> GetRevealStrategy(
        const InteractionGraph& graph,
        const std::string& nodeId);

    static std::optional<NodeIntentBinding> GetIntentBinding(
        const InteractionGraph& graph,
        const std::string& nodeId);

    static GraphDelta ComputeDelta(
        const InteractionGraph& base,
        const InteractionGraph& current);

    static std::string SerializeExecutionPlanJson(const ExecutionPlan& plan);
    static std::string SerializeRevealStrategyJson(const RevealStrategy& reveal);
    static std::string SerializeIntentBindingJson(const NodeIntentBinding& binding);
    static std::string SerializeDeltaJson(const GraphDelta& delta);

    static std::string SerializeNodeJson(const InteractionNode& node);
    static std::string SerializeGraphJson(const InteractionGraph& graph);

private:
    static NodeId BuildNodeId(const UiElement& element, const std::string& uiPath, std::size_t fallbackIndex);
};

}  // namespace iee
