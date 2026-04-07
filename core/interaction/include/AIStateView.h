#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "EnvironmentAdapter.h"

namespace iee {

struct AIActionSummary {
    std::string action;
    std::size_t count{0};
};

struct AIStateView {
    std::uint64_t sequence{0};
    std::uint64_t frameId{0};
    std::uint64_t graphVersion{0};
    std::uint64_t graphSignature{0};
    bool runtimeActive{false};
    std::string activeWindowTitle;
    std::string activeProcessPath;
    std::size_t nodeCount{0};
    std::size_t visibleNodeCount{0};
    std::size_t hiddenNodeCount{0};
    std::size_t actionableNodeCount{0};
    std::size_t commandCount{0};
    std::vector<AIActionSummary> dominantActions;
    std::vector<std::string> hiddenNodeIds;
};

class AIStateViewProjector {
public:
    AIStateView Build(const EnvironmentState& state, bool runtimeActive) const;
    static std::string SerializeJson(const AIStateView& view);
};

}  // namespace iee
