#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "EnvironmentAdapter.h"
#include "EventBus.h"
#include "ExecutionEngine.h"
#include "Intent.h"
#include "IntentRegistry.h"
#include "DecisionInterfaces.h"
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
    int observationIntervalMs{8};
    int decisionBudgetMs{2};
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
    std::uint64_t latestObservationSequence{0};
    std::uint64_t observationSamples{0};
    std::int64_t lastObservationCaptureMs{0};
    std::string observationAdapter;
    std::uint64_t decisionIntentsProduced{0};
    std::uint64_t decisionTimeouts{0};
    std::uint64_t feedbackSamples{0};
    std::uint64_t feedbackMismatches{0};
    std::uint64_t feedbackCorrections{0};
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
    ControlRuntime(
        IntentRegistry& registry,
        ExecutionEngine& executionEngine,
        EventBus& eventBus,
        Telemetry& telemetry,
        std::shared_ptr<EnvironmentAdapter> environmentAdapter = nullptr);
    ~ControlRuntime();

    bool Start(const ControlRuntimeConfig& config, std::string* message = nullptr);
    ControlRuntimeSummary Stop();
    ControlRuntimeSnapshot Status() const;

    bool EnqueueIntent(const Intent& intent, ControlPriority priority);
    bool LatestEnvironmentState(EnvironmentState* state) const;
    void SetDecisionProvider(std::shared_ptr<DecisionProvider> provider, int budgetMs = 2);
    void SetPredictor(std::shared_ptr<Predictor> predictor);
    bool Predict(const Intent& intent, StateSnapshot* predictedState, std::string* diagnostics = nullptr) const;
    std::vector<Feedback> RecentFeedback(std::size_t limit = 32) const;

    static std::string SerializeSnapshotJson(const ControlRuntimeSnapshot& snapshot);
    static std::string SerializeSummaryJson(const ControlRuntimeSummary& summary);

private:
    struct QueuedIntent {
        Intent intent;
        ControlPriority priority{ControlPriority::Low};
        std::uint64_t sequence{0};
        std::chrono::steady_clock::time_point enqueuedAt{std::chrono::steady_clock::now()};
    };

    void RunLoop();
    void HandleEvent(const Event& event);
    void SubmitDecisionState(const EnvironmentState& state, std::uint64_t frame);
    void DecisionLoop();
    bool PopIntentLocked(
        Intent* intent,
        ControlPriority* priority,
        std::chrono::steady_clock::time_point* enqueuedAt = nullptr);
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

    std::shared_ptr<EnvironmentAdapter> environmentAdapter_;
    ObservationPipeline observationPipeline_;
    ObservationPipelineConfig observationConfig_;
    std::uint64_t latestObservationSequence_{0};
    std::uint64_t observationSamples_{0};
    std::int64_t lastObservationCaptureMs_{0};
    std::string observationAdapterName_;

    mutable std::mutex predictorMutex_;
    std::shared_ptr<Predictor> predictor_;

    mutable std::mutex decisionMutex_;
    std::condition_variable decisionCv_;
    std::thread decisionWorker_;
    bool decisionStopRequested_{false};
    bool decisionStatePending_{false};
    EnvironmentState decisionPendingState_;
    std::uint64_t decisionPendingFrame_{0};
    std::shared_ptr<DecisionProvider> decisionProvider_;
    std::chrono::milliseconds decisionBudget_{2};

    std::deque<Feedback> feedbackHistory_;
    std::uint64_t decisionIntentsProduced_{0};
    std::uint64_t decisionTimeouts_{0};
    std::uint64_t feedbackSamples_{0};
    std::uint64_t feedbackMismatches_{0};
    std::uint64_t feedbackCorrections_{0};

    EventBus::SubscriptionId uiChangedSubscriptionId_{0};
    EventBus::SubscriptionId fileChangedSubscriptionId_{0};
    EventBus::SubscriptionId errorSubscriptionId_{0};
    EventBus::SubscriptionId ambiguitySubscriptionId_{0};

    static constexpr std::size_t kMaxLatencySamples = 4096;
    static constexpr std::size_t kMaxFeedbackHistory = 512;
    static constexpr std::size_t kMaxDecisionIntentsPerPass = 8;
};

}  // namespace iee
