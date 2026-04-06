#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "EventBus.h"
#include "ExecutionEngine.h"
#include "Intent.h"
#include "IntentRegistry.h"
#include "Telemetry.h"

namespace iee {

enum class ControlPriority {
    High,
    Medium,
    Low
};

struct ControlRuntimeConfig {
    int targetFrameMs{16};
    std::uint64_t maxFrames{0};
};

struct ControlRuntimeSnapshot {
    bool active{false};
    std::string runtimeId;
    std::uint64_t version{0};
    std::uint64_t framesExecuted{0};
    std::uint64_t intentsExecuted{0};
    std::uint64_t droppedFrames{0};
    std::uint64_t highPriorityEvents{0};
    std::uint64_t mediumPriorityEvents{0};
    std::uint64_t lowPriorityEvents{0};
    std::size_t queuedHigh{0};
    std::size_t queuedMedium{0};
    std::size_t queuedLow{0};
    std::int64_t lastCycleMs{0};
    std::int64_t targetFrameMs{16};
    std::string lastTraceId;
    std::uint64_t latestSnapshotVersion{0};
};

struct ControlRuntimeSummary {
    bool wasActive{false};
    std::string runtimeId;
    std::uint64_t framesExecuted{0};
    std::uint64_t intentsExecuted{0};
    std::uint64_t droppedFrames{0};
    double p50LatencyMs{0.0};
    double p95LatencyMs{0.0};
    double p99LatencyMs{0.0};
};

class ControlRuntime {
public:
    ControlRuntime(IntentRegistry& registry, ExecutionEngine& executionEngine, EventBus& eventBus, Telemetry& telemetry);
    ~ControlRuntime();

    bool Start(const ControlRuntimeConfig& config, std::string* message = nullptr);
    ControlRuntimeSummary Stop();
    ControlRuntimeSnapshot Status() const;

    bool EnqueueIntent(const Intent& intent, ControlPriority priority);

    static std::string SerializeSnapshotJson(const ControlRuntimeSnapshot& snapshot);
    static std::string SerializeSummaryJson(const ControlRuntimeSummary& summary);

private:
    struct QueuedIntent {
        Intent intent;
        ControlPriority priority{ControlPriority::Low};
        std::uint64_t sequence{0};
    };

    void RunLoop();
    void HandleEvent(const Event& event);
    bool PopIntentLocked(Intent* intent, ControlPriority* priority);
    std::array<double, 3> ComputePercentilesLocked() const;

    static std::string EscapeJson(const std::string& value);
    static std::string ToString(ControlPriority priority);

    IntentRegistry& registry_;
    ExecutionEngine& executionEngine_;
    EventBus& eventBus_;
    Telemetry& telemetry_;

    mutable std::mutex mutex_;
    std::condition_variable wakeCv_;
    bool running_{false};
    bool stopRequested_{false};
    std::thread worker_;

    ControlRuntimeConfig config_;
    std::string runtimeId_;
    std::uint64_t nextSequence_{1};
    std::uint64_t version_{0};
    std::uint64_t framesExecuted_{0};
    std::uint64_t intentsExecuted_{0};
    std::uint64_t droppedFrames_{0};
    std::uint64_t highPriorityEvents_{0};
    std::uint64_t mediumPriorityEvents_{0};
    std::uint64_t lowPriorityEvents_{0};
    std::size_t pendingHighEvents_{0};
    std::size_t pendingMediumEvents_{0};
    std::size_t pendingLowEvents_{0};
    bool pendingUiRefresh_{false};
    bool pendingFsRefresh_{false};
    std::int64_t lastCycleMs_{0};
    std::uint64_t latestSnapshotVersion_{0};
    std::string lastTraceId_;

    std::deque<QueuedIntent> highQueue_;
    std::deque<QueuedIntent> mediumQueue_;
    std::deque<QueuedIntent> lowQueue_;
    std::vector<std::int64_t> cycleLatenciesMs_;

    EventBus::SubscriptionId uiChangedSubscriptionId_{0};
    EventBus::SubscriptionId fileChangedSubscriptionId_{0};
    EventBus::SubscriptionId errorSubscriptionId_{0};
    EventBus::SubscriptionId ambiguitySubscriptionId_{0};

    static constexpr std::size_t kMaxLatencySamples = 4096;
};

}  // namespace iee
