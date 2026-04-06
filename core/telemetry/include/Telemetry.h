#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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
    std::uint64_t snapshotVersion{0};
    std::uint64_t controlFrame{0};
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
    std::uint64_t persistedTraceCount{0};
    std::uint64_t droppedPersistenceCount{0};
    std::uint64_t traceBufferSize{0};
    bool traceBufferWrapped{false};
    int rotationFileIndex{0};
    std::vector<AdapterTelemetryMetrics> adapterMetrics;
};

struct TelemetryPersistenceStatus {
    bool enabled{false};
    std::uint64_t queuedCount{0};
    std::uint64_t persistedCount{0};
    std::uint64_t droppedCount{0};
    int currentFileIndex{0};
    std::vector<std::string> files;
};

struct LatencyBreakdownSample {
    std::uint64_t frame{0};
    std::string traceId;
    double observationMs{0.0};
    double perceptionMs{0.0};
    double queueWaitMs{0.0};
    double executionMs{0.0};
    double verificationMs{0.0};
    double totalMs{0.0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct LatencyComponentStats {
    double averageMs{0.0};
    double p95Ms{0.0};
    double maxMs{0.0};
};

struct LatencyBreakdownSnapshot {
    std::uint64_t sampleCount{0};
    LatencyComponentStats observation;
    LatencyComponentStats perception;
    LatencyComponentStats queueWait;
    LatencyComponentStats execution;
    LatencyComponentStats verification;
    LatencyComponentStats total;
    std::optional<LatencyBreakdownSample> latest;
};

struct PerformanceContractSnapshot {
    std::uint64_t sampleCount{0};
    double targetBudgetMs{0.0};
    double p50Ms{0.0};
    double p95Ms{0.0};
    double maxMs{0.0};
    double jitterMs{0.0};
    double driftMs{0.0};
    bool withinBudget{false};
};

class Telemetry {
public:
    Telemetry();
    ~Telemetry();

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
    void LogLatencyBreakdown(const LatencyBreakdownSample& sample);

    std::vector<ExecutionTrace> RecentExecutions(std::size_t limit = 50) const;
    std::vector<ExecutionTrace> QueryExecutions(
        std::size_t limit,
        const std::string& statusFilter,
        const std::string& adapterFilter) const;
    std::optional<ExecutionTrace> FindTrace(const std::string& traceId) const;
    TelemetrySnapshot Snapshot() const;
    TelemetryPersistenceStatus PersistenceStatus() const;
    LatencyBreakdownSnapshot LatencySnapshot(std::size_t limit = 200) const;
    PerformanceContractSnapshot PerformanceContract(double targetBudgetMs, std::size_t limit = 200) const;

    std::string SerializeSnapshotJson() const;
    std::string SerializeTraceJson(const std::string& traceId) const;
    std::string SerializePersistenceJson() const;
    std::string SerializeLatencyJson(std::size_t limit = 200) const;
    std::string SerializePerformanceContractJson(double targetBudgetMs, std::size_t limit = 200) const;

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
    bool traceBufferWrapped_{false};
    std::deque<AdapterDecisionEvent> adapterDecisions_;
    std::deque<std::string> failures_;
    std::deque<LatencyBreakdownSample> latencyBreakdowns_;

    std::unordered_map<std::string, AdapterAggregate> adapterAggregates_;

    std::uint64_t totalExecutions_{0};
    std::uint64_t successCount_{0};
    std::uint64_t failureCount_{0};
    double totalDurationMs_{0.0};

    std::uint64_t resolutionSamples_{0};
    double totalResolutionMs_{0.0};

    mutable std::mutex persistenceMutex_;
    std::condition_variable persistenceCv_;
    std::thread persistenceThread_;
    std::deque<ExecutionTrace> persistenceQueue_;
    bool persistenceStopRequested_{false};
    bool persistenceEnabled_{false};
    std::filesystem::path persistenceDirectory_;
    std::uint64_t persistedTraceCount_{0};
    std::uint64_t droppedPersistenceCount_{0};
    int currentFileIndex_{0};
    std::size_t currentFileBytes_{0};
    std::deque<std::string> persistedFiles_;

    void StartPersistenceWorker();
    void StopPersistenceWorker();
    void PersistenceLoop();
    void EnqueuePersistence(const ExecutionTrace& trace);
    void PersistTraceLine(const ExecutionTrace& trace);
    std::filesystem::path CurrentTraceFilePathLocked() const;

    static constexpr std::size_t kMaxPersistedFiles = 8;
    static constexpr std::size_t kMaxTraceFileBytes = 1024 * 1024;
    static constexpr std::size_t kMaxPersistenceQueue = 4096;
    static constexpr std::size_t kMaxLatencySamples = 4096;
};

}  // namespace iee
