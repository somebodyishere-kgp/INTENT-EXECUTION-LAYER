#pragma once

#include <Windows.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ObserverEngine.h"

namespace iee {

struct CapabilityNode {
    std::string id;
    std::string type;
    std::vector<std::string> capabilities;
    std::vector<std::string> relations;
    std::string parentId;
    std::vector<std::string> childIds;
    std::wstring label;
    std::wstring contextApp;
    std::wstring contextWindow;
    std::wstring role;
    RECT bounds{0, 0, 0, 0};
    int hierarchyDepth{0};
    bool focused{false};
};

class CapabilityGraph {
public:
    void Clear();
    void AddNode(const CapabilityNode& node);
    void AddRelation(const std::string& fromId, const std::string& toId);

    const std::map<std::string, CapabilityNode>& Nodes() const;
    std::optional<CapabilityNode> FindNode(const std::string& id) const;
    std::vector<CapabilityNode> ChildrenOf(const std::string& id) const;
    std::size_t Size() const;

private:
    std::map<std::string, CapabilityNode> nodes_;
};

class CapabilityGraphBuilder {
public:
    CapabilityGraph Build(const ObserverSnapshot& snapshot) const;
};

}  // namespace iee
