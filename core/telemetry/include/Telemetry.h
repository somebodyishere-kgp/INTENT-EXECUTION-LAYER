#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Intent.h"

namespace iee {

struct ExecutionTrace {
    std::string traceId;
    std::string intent;
    std::wstring target;
    std::string adapter;
    std::int64_t durationMs{0};
    std::string status;
    std::string message;
    bool verified{false};
    int attempts{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct AdapterTelemetryMetrics {
    std::string adapter;
    std::uint64_t executions{0};
    std::uint64_t successes{0};
    double successRate{0.0};
    double averageLatencyMs{0.0};
};

struct TelemetrySnapshot {
    std::uint64_t totalExecutions{0};
    std::uint64_t successCount{0};
    std::uint64_t failureCount{0};
    double successRate{0.0};
    double averageLatencyMs{0.0};
    double averageResolutionMs{0.0};
    std::uint64_t uptimeMs{0};
    std::vector<AdapterTelemetryMetrics> adapterMetrics;
};

class Telemetry {
public:
    Telemetry();

    std::string NewTraceId();

    void LogExecution(const ExecutionTrace& trace);
    void LogAdapterDecision(
        const std::string& traceId,
        IntentAction action,
        const std::wstring& target,
        const std::string& selectedAdapter,
        const std::vector<std::pair<std::string, float>>& rankedScores,
        bool fastPath);
    void LogFailure(const std::string& traceId, const std::string& component, const std::string& message);
    void LogResolutionTiming(std::chrono::milliseconds duration);

    std::vector<ExecutionTrace> RecentExecutions(std::size_t limit = 50) const;
    std::optional<ExecutionTrace> FindTrace(const std::string& traceId) const;
    TelemetrySnapshot Snapshot() const;

    std::string SerializeSnapshotJson() const;
    std::string SerializeTraceJson(const std::string& traceId) const;

private:
    struct AdapterAggregate {
        std::uint64_t executions{0};
        std::uint64_t successes{0};
        double totalLatencyMs{0.0};
    };

    struct AdapterDecisionEvent {
        std::string traceId;
        std::string action;
        std::wstring target;
        std::string selectedAdapter;
        std::vector<std::pair<std::string, float>> rankedScores;
        bool fastPath{false};
        std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    };

    static constexpr std::size_t kMaxTraces = 2048;
    static constexpr std::size_t kMaxDecisions = 2048;
    static constexpr std::size_t kMaxFailures = 1024;

    mutable std::mutex mutex_;
    std::uint64_t nextTraceCounter_{0};
    std::chrono::steady_clock::time_point startedAt_;

    std::deque<ExecutionTrace> traces_;
    std::deque<AdapterDecisionEvent> adapterDecisions_;
    std::deque<std::string> failures_;

    std::unordered_map<std::string, AdapterAggregate> adapterAggregates_;

    std::uint64_t totalExecutions_{0};
    std::uint64_t successCount_{0};
    std::uint64_t failureCount_{0};
    double totalDurationMs_{0.0};

    std::uint64_t resolutionSamples_{0};
    double totalResolutionMs_{0.0};
};

}  // namespace iee
