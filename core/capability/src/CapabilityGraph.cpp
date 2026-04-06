#include "CapabilityGraph.h"

#include <filesystem>
#include <sstream>
#include <unordered_map>

namespace iee {
namespace {

std::string ControlTypeToString(UiControlType type) {
    switch (type) {
    case UiControlType::Window:
        return "window";
    case UiControlType::Button:
        return "button";
    case UiControlType::TextBox:
        return "textbox";
    case UiControlType::Menu:
        return "menu";
    case UiControlType::MenuItem:
        return "menu_item";
    case UiControlType::ComboBox:
        return "combobox";
    case UiControlType::ListItem:
        return "list_item";
    case UiControlType::Document:
        return "document";
    case UiControlType::Unknown:
    default:
        return "unknown";
    }
}

std::vector<std::string> CapabilitiesForElement(const UiElement& element) {
    std::vector<std::string> capabilities;

    if (element.supportsInvoke) {
        capabilities.push_back("activate");
    }
    if (element.supportsValue) {
        capabilities.push_back("set_value");
    }
    if (element.supportsSelection || element.controlType == UiControlType::Menu ||
        element.controlType == UiControlType::MenuItem || element.controlType == UiControlType::ComboBox ||
        element.controlType == UiControlType::ListItem) {
        capabilities.push_back("select");
    }

    return capabilities;
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

std::wstring Wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int requiredChars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (requiredChars <= 1) {
        return L"";
    }

    std::wstring result(static_cast<std::size_t>(requiredChars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), requiredChars);
    result.pop_back();
    return result;
}

}  // namespace

void CapabilityGraph::Clear() {
    nodes_.clear();
}

void CapabilityGraph::AddNode(const CapabilityNode& node) {
    nodes_[node.id] = node;
}

void CapabilityGraph::AddRelation(const std::string& fromId, const std::string& toId) {
    auto fromIt = nodes_.find(fromId);
    auto toIt = nodes_.find(toId);
    if (fromIt == nodes_.end() || toIt == nodes_.end()) {
        return;
    }

    fromIt->second.childIds.push_back(toId);
    fromIt->second.relations.push_back(toId);
    toIt->second.parentId = fromId;
}

const std::map<std::string, CapabilityNode>& CapabilityGraph::Nodes() const {
    return nodes_;
}

std::optional<CapabilityNode> CapabilityGraph::FindNode(const std::string& id) const {
    const auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::vector<CapabilityNode> CapabilityGraph::ChildrenOf(const std::string& id) const {
    std::vector<CapabilityNode> children;
    const auto parent = nodes_.find(id);
    if (parent == nodes_.end()) {
        return children;
    }

    for (const auto& childId : parent->second.childIds) {
        const auto child = nodes_.find(childId);
        if (child != nodes_.end()) {
            children.push_back(child->second);
        }
    }

    return children;
}

std::size_t CapabilityGraph::Size() const {
    return nodes_.size();
}

CapabilityGraph CapabilityGraphBuilder::Build(const ObserverSnapshot& snapshot) const {
    CapabilityGraph graph;

    CapabilityNode windowNode;
    windowNode.id = "window:" + std::to_string(reinterpret_cast<std::uintptr_t>(snapshot.activeWindow));
    windowNode.type = "window";
    windowNode.label = snapshot.activeWindowTitle;
    windowNode.contextApp = snapshot.activeProcessPath;
    windowNode.contextWindow = snapshot.activeWindowTitle;
    windowNode.capabilities = {"activate", "select"};
    windowNode.role = L"window";
    graph.AddNode(windowNode);

    std::unordered_map<std::string, std::string> depthAnchors;
    depthAnchors["0"] = windowNode.id;

    for (const auto& element : snapshot.uiElements) {
        CapabilityNode node;
        node.id = element.id;
        node.type = ControlTypeToString(element.controlType);
        node.label = !element.name.empty() ? element.name : element.automationId;
        node.contextApp = snapshot.activeProcessPath;
        node.contextWindow = snapshot.activeWindowTitle;
        node.capabilities = CapabilitiesForElement(element);
        node.role = Wide(ControlTypeToString(element.controlType));
        node.bounds = element.bounds;
        node.hierarchyDepth = element.depth;
        node.focused = element.isFocused;

        graph.AddNode(node);

        const std::string parentKey = std::to_string(element.depth - 1);
        const auto parentAnchor = depthAnchors.find(parentKey);
        const std::string parentId = !element.parentId.empty() ? element.parentId : (parentAnchor != depthAnchors.end() ? parentAnchor->second : windowNode.id);
        graph.AddRelation(parentId, node.id);

        depthAnchors[std::to_string(element.depth)] = node.id;
    }

    for (const auto& entry : snapshot.fileSystemEntries) {
        CapabilityNode node;
        node.id = "fs:" + Narrow(entry.path);
        node.type = entry.isDirectory ? "directory" : "file";
        node.label = entry.path;
        node.contextApp = snapshot.activeProcessPath;
        node.contextWindow = snapshot.activeWindowTitle;
        node.role = entry.isDirectory ? L"directory" : L"file";
        node.capabilities = entry.isDirectory ? std::vector<std::string>{"create", "move", "delete"}
                                              : std::vector<std::string>{"move", "delete"};
        node.hierarchyDepth = 0;

        graph.AddNode(node);
    }

    return graph;
}

}  // namespace iee
