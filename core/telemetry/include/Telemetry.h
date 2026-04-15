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
    std::uint64_t reflexSamples{0};
    std::uint64_t reflexIntentsProduced{0};
    double reflexAverageDecisionMs{0.0};
    double reflexP95DecisionMs{0.0};
    double reflexAverageLoopMs{0.0};
    std::uint64_t reflexOverBudgetCount{0};
    std::uint64_t reflexExploratoryCount{0};
    std::uint64_t reflexGoalConditionedCount{0};
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

struct LatencyPercentilesSnapshot {
    std::uint64_t sampleCount{0};
    double p50Ms{0.0};
    double p95Ms{0.0};
    double p99Ms{0.0};
    double p999Ms{0.0};
};

struct VisionLatencySample {
    std::uint64_t frameId{0};
    std::uint64_t environmentSequence{0};
    double captureMs{0.0};
    double detectionMs{0.0};
    double mergeMs{0.0};
    double totalMs{0.0};
    bool simulated{false};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct VisionComponentStats {
    double averageMs{0.0};
    double p95Ms{0.0};
    double maxMs{0.0};
};

struct VisionSnapshot {
    std::uint64_t sampleCount{0};
    std::uint64_t simulatedSamples{0};
    std::uint64_t droppedFrames{0};
    double estimatedFps{0.0};
    VisionComponentStats capture;
    VisionComponentStats detection;
    VisionComponentStats merge;
    VisionComponentStats total;
    std::optional<VisionLatencySample> latest;
};

struct ReflexTelemetrySample {
    std::uint64_t frame{0};
    std::int64_t decisionTimeUs{0};
    std::int64_t loopTimeUs{0};
    float priority{0.0F};
    bool decisionWithinBudget{true};
    bool exploratory{false};
    bool executable{false};
    bool intentProduced{false};
    bool goalConditioned{false};
    std::string reason;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

struct ReflexTelemetrySnapshot {
    std::uint64_t sampleCount{0};
    std::uint64_t intentsProduced{0};
    double averageDecisionMs{0.0};
    double p95DecisionMs{0.0};
    double averageLoopMs{0.0};
    std::uint64_t overBudgetCount{0};
    std::uint64_t exploratoryCount{0};
    std::uint64_t goalConditionedCount{0};
    std::optional<ReflexTelemetrySample> latest;
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
    void LogVisionSample(const VisionLatencySample& sample);
    void LogReflexSample(const ReflexTelemetrySample& sample);

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
    LatencyPercentilesSnapshot LatencyPercentiles(std::size_t limit = 200) const;
    VisionSnapshot VisionLatencySnapshot(std::size_t limit = 200) const;
    ReflexTelemetrySnapshot ReflexSnapshot(std::size_t limit = 256) const;

    std::string SerializeSnapshotJson() const;
    std::string SerializeTraceJson(const std::string& traceId) const;
    std::string SerializePersistenceJson() const;
    std::string SerializeLatencyJson(std::size_t limit = 200) const;
    std::string SerializePerformanceContractJson(double targetBudgetMs, std::size_t limit = 200) const;
    std::string SerializeLatencyPercentilesJson(std::size_t limit = 200) const;
    std::string SerializeVisionJson(std::size_t limit = 200) const;
    std::string SerializeReflexJson(std::size_t limit = 256) const;

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
    std::deque<VisionLatencySample> visionSamples_;
    std::deque<ReflexTelemetrySample> reflexSamples_;

    std::unordered_map<std::string, AdapterAggregate> adapterAggregates_;

    std::uint64_t totalExecutions_{0};
    std::uint64_t successCount_{0};
    std::uint64_t failureCount_{0};
    double totalDurationMs_{0.0};

    std::uint64_t resolutionSamples_{0};
    double totalResolutionMs_{0.0};

    std::uint64_t visionSimulatedSamples_{0};
    std::uint64_t visionDroppedFrames_{0};
    std::uint64_t latestVisionFrameId_{0};

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
    static constexpr std::size_t kMaxVisionSamples = 4096;
    static constexpr std::size_t kMaxReflexSamples = 4096;
};

}  // namespace iee
