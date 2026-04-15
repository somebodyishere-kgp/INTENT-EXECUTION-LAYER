#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "UniversalReflexEngine.h"

namespace iee {

struct Vec2 {
    float x{0.0F};
    float y{0.0F};
};

struct ContinuousAction {
    float move_x{0.0F};
    float move_y{0.0F};
    float aim_dx{0.0F};
    float aim_dy{0.0F};
    float look_dx{0.0F};
    float look_dy{0.0F};
    bool fire{false};
    bool interact{false};
};

struct ReflexBundle {
    std::string source;
    std::string target_object_id;
    std::vector<Action> discrete_actions;
    std::vector<ContinuousAction> continuous_actions;
    float priority{0.0F};
};

struct CoordinatedOutput {
    ContinuousAction continuous;
    std::vector<Action> discrete;
};

struct AttentionMap {
    std::vector<WorldObject> focus_objects;
};

struct PredictedState {
    std::string object_id;
    Vec2 future_position;
    float confidence{0.0F};
};

struct Skill {
    std::string name;
    std::vector<Action> sequence;
    std::uint64_t attempts{0};
    std::uint64_t success_count{0};
    std::int64_t updated_at_ms{0};
};

class ContinuousController {
public:
    explicit ContinuousController(float smoothingAlpha = 0.35F, float sensitivity = 1.0F);

    ContinuousAction Apply(const ContinuousAction& target);
    void Reset();

private:
    static float ClampUnit(float value);

    float smoothingAlpha_{0.35F};
    float sensitivity_{1.0F};
    ContinuousAction previous_{};
    bool hasPrevious_{false};
};

class ActionCoordinator {
public:
    CoordinatedOutput resolve(const std::vector<ReflexBundle>& bundles) const;
};

class MovementAgent {
public:
    ReflexBundle Propose(
        const WorldModel& model,
        const AttentionMap& attention,
        const ReflexGoal* goal,
        const ReflexSafetyPolicy& safety) const;
};

class AimAgent {
public:
    ReflexBundle Propose(
        const WorldModel& model,
        const AttentionMap& attention,
        const ReflexGoal* goal,
        const ReflexSafetyPolicy& safety) const;
};

class InteractionAgent {
public:
    ReflexBundle Propose(
        const WorldModel& model,
        const AttentionMap& attention,
        const ReflexGoal* goal,
        const ReflexSafetyPolicy& safety) const;
};

class StrategyAgent {
public:
    ReflexBundle Propose(
        const WorldModel& model,
        const AttentionMap& attention,
        const ReflexGoal* goal,
        const ReflexSafetyPolicy& safety) const;
};

class MicroPlanner {
public:
    std::vector<ReflexBundle> refine(const WorldModel& model, const ReflexGoal& goal) const;
    std::vector<ReflexBundle> refine(
        const WorldModel& model,
        const ReflexGoal& goal,
        const std::vector<ReflexBundle>& bundles) const;
};

class SkillMemoryStore {
public:
    explicit SkillMemoryStore(std::filesystem::path filePath = {});

    bool Load();
    bool Save() const;
    void Record(const std::string& name, const std::vector<Action>& sequence, bool success);
    std::vector<Skill> Skills(std::size_t limit = 64U) const;

private:
    std::filesystem::path filePath_;
    std::map<std::string, Skill> skills_;
};

AttentionMap BuildAttentionMap(const WorldModel& model, std::size_t maxObjects = 4U);
std::unordered_map<std::string, Vec2> BuildObjectCenters(const WorldModel& model);
std::vector<PredictedState> BuildPredictedStates(
    const WorldModel& model,
    const std::unordered_map<std::string, Vec2>& previousCenters,
    std::uint64_t horizonFrames = 3U);

std::string SerializeContinuousActionJson(const ContinuousAction& action);
std::string SerializeReflexBundleJson(const ReflexBundle& bundle);
std::string SerializeReflexBundlesJson(const std::vector<ReflexBundle>& bundles);
std::string SerializeCoordinatedOutputJson(const CoordinatedOutput& output);
std::string SerializeAttentionMapJson(const AttentionMap& attention);
std::string SerializePredictedStatesJson(const std::vector<PredictedState>& predictions);
std::string SerializeSkillJson(const Skill& skill);
std::string SerializeSkillsJson(const std::vector<Skill>& skills);

}  // namespace iee
