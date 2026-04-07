#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "InteractionGraph.h"

namespace iee {

enum class TaskDomain {
    Generic,
    Presentation,
    Browser
};

struct TaskRequest {
    std::string goal;
    std::string targetHint;
    TaskDomain domain{TaskDomain::Generic};
    bool allowHidden{true};
    std::size_t maxPlans{3};
};

struct TaskPlanCandidate {
    std::string nodeId;
    std::string label;
    std::string action;
    double score{0.0};
    bool hidden{false};
    bool requiresReveal{false};
    ExecutionPlan executionPlan;
    RevealStrategy revealStrategy;
};

struct TaskPlanResult {
    std::string taskId;
    std::string goal;
    std::string summary;
    bool deterministic{true};
    std::vector<TaskPlanCandidate> candidates;
};

class TaskPlanner {
public:
    TaskPlanResult Plan(const TaskRequest& request, const InteractionGraph& graph) const;

    static TaskDomain ParseDomain(const std::string& value);
    static std::string ToString(TaskDomain domain);
    static std::string SerializeJson(const TaskPlanResult& result);
};

}  // namespace iee
