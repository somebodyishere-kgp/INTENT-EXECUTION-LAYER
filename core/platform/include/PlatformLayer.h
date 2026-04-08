#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "AIStateView.h"
#include "ActionInterface.h"
#include "EnvironmentAdapter.h"
#include "ExecutionEngine.h"
#include "IntentRegistry.h"
#include "TaskInterface.h"
#include "Telemetry.h"

namespace iee {

struct PermissionPolicy {
    bool allow_execute{true};
    bool allow_file_ops{true};
    bool allow_system_changes{false};
};

struct PermissionCheckResult {
    bool allowed{true};
    std::string reason{"allowed"};
};

class PermissionPolicyStore {
public:
    static PermissionPolicy Get();
    static PermissionPolicy Apply(const PermissionPolicy& policy);
    static PermissionCheckResult Check(const Intent& intent);
    static std::string SerializeJson(const PermissionPolicy& policy);
};

struct SuccessStats {
    std::uint64_t successCount{0};
    std::uint64_t failureCount{0};
    std::uint64_t fallbackUsageCount{0};
    double averageLatencyMs{0.0};
};

struct ExecutionMemory {
    std::unordered_map<std::string, SuccessStats> stats;
};

class ExecutionMemoryStore {
public:
    static void Record(const std::string& nodeId, const ActionExecutionResult& result);
    static double SuccessBias(const std::string& nodeId);
    static std::string SerializeJson(std::size_t limit = 128U);
};

struct StateHistory {
    std::deque<UnifiedState> history;
};

struct StateTransitionInfo {
    bool changed{false};
    bool uiChanged{false};
    bool stable{false};
    std::uint64_t fromFrame{0};
    std::uint64_t toFrame{0};
    std::int64_t elapsedMs{0};
};

struct FrameConsistencyMetrics {
    std::uint64_t expectedFrames{0};
    std::uint64_t actualFrames{0};
    std::uint64_t skippedFrames{0};
    double score{0.0};
};

class TemporalStateEngine {
public:
    void Record(const EnvironmentState& state);
    StateHistory Snapshot(std::size_t limit = 64U) const;
    StateTransitionInfo LatestTransition() const;
    bool IsStable(std::size_t minStableSamples = 2U) const;
    FrameConsistencyMetrics FrameConsistency(std::size_t limit = 64U) const;
    std::string SerializeJson(std::size_t limit = 64U) const;

private:
    mutable std::mutex mutex_;
    std::deque<UnifiedState> history_;
    std::deque<StateTransitionInfo> transitions_;
};

struct IntentSequence {
    std::vector<ActionRequest> steps;
};

struct IntentSequenceStepTrace {
    std::size_t index{0};
    ActionExecutionResult result;
};

struct IntentSequenceExecutionResult {
    std::string status{"failure"};
    std::string traceId;
    std::size_t attemptedSteps{0};
    std::size_t completedSteps{0};
    int failedStep{-1};
    std::vector<IntentSequenceStepTrace> stepTraces;
    std::string reason;
};

class IntentSequenceExecutor {
public:
    IntentSequenceExecutor(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry);

    IntentSequenceExecutionResult Execute(const IntentSequence& sequence, bool stopOnFailure = true);

private:
    IntentRegistry& registry_;
    ExecutionEngine& executionEngine_;
    Telemetry& telemetry_;
};

class WorkflowExecutor {
public:
    WorkflowExecutor(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry);

    IntentSequenceExecutionResult runWorkflow(IntentSequence& sequence);

private:
    IntentSequenceExecutor executor_;
};

struct SemanticTaskRequest {
    std::string goal;
    ActionContextHints context;
};

struct SemanticPlanResult {
    std::string mode{"task_request"};
    TaskRequest taskRequest;
    IntentSequence intentSequence;
    bool sequenceGenerated{false};
    std::string diagnostics;
};

class SemanticPlannerBridge {
public:
    static bool ParseSemanticTaskRequestJson(std::string_view payload, SemanticTaskRequest* request, std::string* error);
    static SemanticPlanResult Plan(const SemanticTaskRequest& request);
    static std::string SerializePlanJson(const SemanticPlanResult& plan);
};

bool ParseIntentSequenceJson(std::string_view payload, IntentSequence* sequence, std::string* error);
std::string SerializeIntentSequenceExecutionResultJson(const IntentSequenceExecutionResult& result);
std::string SerializeUcpActEnvelope(const ActionExecutionResult& result);
std::string SerializeUcpStateEnvelope(const AIStateView& stateView);

}  // namespace iee
