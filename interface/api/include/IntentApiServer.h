#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "EnvironmentAdapter.h"
#include "ControlRuntime.h"
#include "DecisionInterfaces.h"
#include "ExecutionEngine.h"
#include "InteractionGraph.h"
#include "IntentRegistry.h"
#include "PlatformLayer.h"
#include "UniversalReflexEngine.h"
#include "ReflexCoordination.h"
#include "Telemetry.h"

namespace iee {

class IntentApiServer {
public:
    IntentApiServer(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry);
    void SetPredictor(std::shared_ptr<Predictor> predictor);

    int Run(std::uint16_t port, bool singleRequest = false, std::size_t maxRequests = 0);
    std::string HandleRequestForTesting(const std::string& request);

private:
    struct UreRuntimeState {
        bool active{false};
        bool executeActions{false};
        bool demoMode{false};
        bool autoPriority{true};
        std::int64_t decisionBudgetUs{1000};
        ControlPriority priority{ControlPriority::Medium};
        std::uint64_t framesEvaluated{0};
        std::uint64_t intentsProduced{0};
        std::uint64_t executionAttempts{0};
        std::uint64_t executionSuccesses{0};
        std::uint64_t executionFailures{0};
        std::uint64_t goalVersion{0};
        std::string lastTraceId;
        std::string lastReason;
        std::int64_t lastDecisionTimeUs{0};
        std::int64_t lastLoopTimeUs{0};
        bool lastGoalConditioned{false};
        std::uint64_t bundleFrames{0};
        std::uint64_t coordinatedActions{0};
        std::uint64_t skillHierarchyFrames{0};
        std::uint64_t anticipationFrames{0};
        std::uint64_t strategyFrames{0};
        std::uint64_t preemptedFrames{0};
        std::chrono::system_clock::time_point startedAt{std::chrono::system_clock::now()};
        std::chrono::system_clock::time_point lastTickAt{std::chrono::system_clock::time_point{}};
        ReflexGoal goal;
        AttentionMap attention;
        std::vector<PredictedState> predictions;
        std::vector<ReflexBundle> bundles;
        CoordinatedOutput coordinatedOutput;
        std::vector<Skill> rankedSkills;
        std::vector<SkillNode> skillHierarchy;
        AnticipationSignal anticipation;
        TemporalStrategyPlan strategy;
        PreemptionDecision preemption;
    };

    std::string HandleRequest(const std::string& request);
    ControlRuntime& EnsureControlRuntime();
    std::string SerializeUreStatusJson() const;
    bool RestoreUrePersistentState();
    void PersistUrePersistentState();

    IntentRegistry& registry_;
    ExecutionEngine& executionEngine_;
    Telemetry& telemetry_;
    std::unique_ptr<ControlRuntime> controlRuntime_;
    std::shared_ptr<EnvironmentAdapter> streamEnvironmentAdapter_;
    mutable std::mutex predictorMutex_;
    std::shared_ptr<Predictor> predictor_;
    std::chrono::steady_clock::time_point startedAt_;
    mutable std::mutex frameHistoryMutex_;
    std::deque<ScreenState> frameHistory_;
    mutable std::mutex graphHistoryMutex_;
    std::deque<InteractionGraph> graphHistory_;
    TemporalStateEngine temporalStateEngine_;
    mutable std::mutex reflexMutex_;
    UniversalReflexAgent reflexAgent_;
    SkillMemoryStore skillMemoryStore_;
    mutable std::mutex ureRuntimeMutex_;
    UreRuntimeState ureRuntime_;
};

}  // namespace iee
