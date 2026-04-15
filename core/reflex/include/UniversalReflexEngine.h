#pragma once

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "EnvironmentAdapter.h"

namespace iee {

struct BoundingBox {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
};

struct UniversalFeature {
    std::string id;
    std::string type;
    BoundingBox bbox;
    bool actionable{false};
    bool dynamic{false};
    float salience{0.0F};
    std::string label;
    std::string source;
};

struct Relationship {
    std::string fromId;
    std::string toId;
    std::string type;
    float strength{0.0F};
};

struct WorldObject {
    std::string id;
    std::string type;
    BoundingBox bbox;
    std::vector<std::string> affordances;
    std::string label;
    bool actionable{false};
    bool dynamic{false};
    float salience{0.0F};
    std::uint64_t lastSeenFrame{0};
};

struct WorldModel {
    std::uint64_t frameId{0};
    std::uint64_t signature{0};
    std::unordered_map<std::string, WorldObject> objects;
    std::vector<Relationship> relationships;
    std::size_t changedObjects{0};
};

struct Affordance {
    std::string object_id;
    std::vector<std::string> actions;
    float confidence{0.0F};
};

struct Condition {
    std::string kind;
    std::string objectType;
};

struct Action {
    std::string name;
    std::string intentAction;
};

struct PolicyRule {
    Condition condition;
    Action action;
    float priority{0.0F};
};

struct ReflexSafetyPolicy {
    bool allowExecute{true};
    bool allowFileOps{true};
    bool allowSystemChanges{false};
    bool allowExploration{true};
};

struct ExplorationResult {
    std::string objectId;
    std::string action;
    bool success{false};
    float reward{0.0F};
};

struct WorldState {
    std::uint64_t signature{0};
    std::size_t objects{0};
    std::size_t relationships{0};
};

struct ReflexDecision {
    std::string objectId;
    std::string objectType;
    std::string action;
    std::string targetLabel;
    std::string reason;
    float priority{0.0F};
    bool executable{false};
    bool exploratory{false};
};

struct ReflexGoal {
    std::string goal;
    std::string target;
    std::string domain;
    std::vector<std::string> preferredActions;
    bool active{false};
    std::int64_t updatedAtMs{0};
};

struct ExperienceEntry {
    WorldState state;
    ReflexDecision action;
    float reward{0.0F};
    std::int64_t timestampMs{0};
};

struct ReflexMetricsSnapshot {
    std::uint64_t decisions{0};
    std::uint64_t loops{0};
    double averageDecisionMs{0.0};
    double p95DecisionMs{0.0};
    double averageLoopMs{0.0};
    std::uint64_t overBudgetDecisions{0};
    std::uint64_t exploratoryDecisions{0};
};

struct ReflexStepResult {
    std::vector<UniversalFeature> features;
    WorldModel worldModel;
    std::vector<Affordance> affordances;
    ReflexDecision decision;
    std::vector<ExplorationResult> exploration;
    std::int64_t decisionTimeUs{0};
    std::int64_t loopTimeUs{0};
    bool decisionWithinBudget{true};
};

class UniversalFeatureExtractor {
public:
    static std::vector<UniversalFeature> Extract(const EnvironmentState& state, const POINT* previousCursor = nullptr);
};

class WorldModelBuilder {
public:
    WorldModel Build(
        const std::vector<UniversalFeature>& features,
        const WorldModel* previousModel,
        std::uint64_t frameId) const;
};

class AffordanceEngine {
public:
    static std::vector<Affordance> Infer(const WorldModel& model);
};

class MetaPolicyEngine {
public:
    MetaPolicyEngine();

    const std::vector<PolicyRule>& Rules() const {
        return rules_;
    }

    ReflexDecision Decide(
        const WorldModel& model,
        const std::vector<Affordance>& affordances,
        const ReflexSafetyPolicy& safety,
        const std::unordered_map<std::string, float>& failureBias,
        const ReflexGoal* goal = nullptr) const;

private:
    std::vector<PolicyRule> rules_;
};

class ExplorationEngine {
public:
    static std::vector<ExplorationResult> Propose(
        const WorldModel& model,
        const std::vector<Affordance>& affordances,
        const ReflexSafetyPolicy& safety,
        std::size_t maxResults = 2U);
};

class UniversalReflexAgent {
public:
    UniversalReflexAgent();

    ReflexStepResult Step(
        const EnvironmentState& state,
        const ReflexSafetyPolicy& safety,
        std::int64_t decisionBudgetUs = 1000,
        const ReflexGoal* goal = nullptr);

    void RecordExecutionOutcome(const ReflexDecision& decision, bool success, std::optional<float> reward = std::nullopt);

    ReflexMetricsSnapshot Metrics() const;
    std::vector<ExperienceEntry> Experience(std::size_t limit = 64U) const;
    void RestoreExperience(const std::vector<ExperienceEntry>& entries);

private:
    static std::string BiasKey(const ReflexDecision& decision);

    WorldModelBuilder worldBuilder_;
    MetaPolicyEngine policyEngine_;

    WorldModel previousWorldModel_;
    POINT previousCursor_{0, 0};
    bool hasPreviousCursor_{false};

    std::deque<ExperienceEntry> experience_;
    std::unordered_map<std::string, float> failureBiasByKey_;
    std::unordered_map<std::string, float> affordanceConfidenceAdjustments_;

    std::deque<double> decisionDurationsMs_;
    std::deque<double> loopDurationsMs_;

    std::uint64_t decisionCount_{0};
    std::uint64_t loopCount_{0};
    std::uint64_t overBudgetDecisions_{0};
    std::uint64_t exploratoryDecisions_{0};
};

std::string SerializeBoundingBoxJson(const BoundingBox& bbox);
std::string SerializeUniversalFeaturesJson(const std::vector<UniversalFeature>& features);
std::string SerializeWorldModelJson(const WorldModel& worldModel);
std::string SerializeAffordancesJson(const std::vector<Affordance>& affordances);
std::string SerializeReflexDecisionJson(const ReflexDecision& decision);
std::string SerializeExplorationJson(const std::vector<ExplorationResult>& exploration);
std::string SerializeReflexStepResultJson(const ReflexStepResult& step);
std::string SerializeReflexMetricsJson(const ReflexMetricsSnapshot& metrics);
std::string SerializeExperienceEntriesJson(const std::vector<ExperienceEntry>& entries);
std::string SerializeReflexGoalJson(const ReflexGoal& goal);

}  // namespace iee
