#include "ReflexCoordination.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace iee {
namespace {

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 16U);

    for (const char ch : value) {
        switch (ch) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

std::string ToAsciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
}

std::vector<std::string> TokenizeLower(const std::string& value) {
    const std::string normalized = ToAsciiLower(value);

    std::vector<std::string> tokens;
    std::string token;

    for (const char ch : normalized) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            token.push_back(ch);
            continue;
        }

        if (!token.empty()) {
            tokens.push_back(std::move(token));
            token.clear();
        }
    }

    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }

    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

bool TokenSetContains(const std::vector<std::string>& haystack, const std::string& value) {
    if (value.empty()) {
        return false;
    }

    return std::binary_search(haystack.begin(), haystack.end(), ToAsciiLower(value));
}

float ClampUnit(float value) {
    return std::clamp(value, -1.0F, 1.0F);
}

float ClampPriority(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

Vec2 CenterOf(const BoundingBox& bbox) {
    Vec2 center;
    center.x = static_cast<float>(bbox.left + bbox.right) * 0.5F;
    center.y = static_cast<float>(bbox.top + bbox.bottom) * 0.5F;
    return center;
}

std::optional<WorldObject> SelectAttentionObject(const WorldModel& model, const AttentionMap& attention) {
    if (!attention.focus_objects.empty()) {
        return attention.focus_objects.front();
    }

    std::optional<WorldObject> best;
    for (const auto& entry : model.objects) {
        const WorldObject& object = entry.second;
        if (!best.has_value()) {
            best = object;
            continue;
        }

        if (std::abs(object.salience - best->salience) > 0.0001F) {
            if (object.salience > best->salience) {
                best = object;
            }
            continue;
        }

        if (object.id < best->id) {
            best = object;
        }
    }

    return best;
}

Vec2 NormalizedDirectionToObject(const WorldModel& model, const WorldObject& object) {
    if (model.objects.empty()) {
        return {};
    }

    int minLeft = object.bbox.left;
    int minTop = object.bbox.top;
    int maxRight = object.bbox.right;
    int maxBottom = object.bbox.bottom;

    for (const auto& entry : model.objects) {
        const BoundingBox& bbox = entry.second.bbox;
        minLeft = (std::min)(minLeft, bbox.left);
        minTop = (std::min)(minTop, bbox.top);
        maxRight = (std::max)(maxRight, bbox.right);
        maxBottom = (std::max)(maxBottom, bbox.bottom);
    }

    const float width = static_cast<float>((std::max)(1, maxRight - minLeft));
    const float height = static_cast<float>((std::max)(1, maxBottom - minTop));

    const Vec2 center = CenterOf(object.bbox);
    const float originX = static_cast<float>(minLeft) + (width * 0.5F);
    const float originY = static_cast<float>(minTop) + (height * 0.5F);

    Vec2 normalized;
    normalized.x = ClampUnit((center.x - originX) / (width * 0.5F));
    normalized.y = ClampUnit((center.y - originY) / (height * 0.5F));
    return normalized;
}

Action BuildAction(std::string name, std::string intentAction) {
    Action action;
    action.name = std::move(name);
    action.intentAction = std::move(intentAction);
    return action;
}

bool IsMeaningfulGoal(const ReflexGoal* goal) {
    return goal != nullptr && goal->active && !goal->goal.empty();
}

bool GoalTouchesObject(const ReflexGoal* goal, const WorldObject& object) {
    if (!IsMeaningfulGoal(goal)) {
        return false;
    }

    const std::vector<std::string> objectTokens = TokenizeLower(object.id + " " + object.type + " " + object.label);
    const std::vector<std::string> goalTokens = TokenizeLower(goal->goal + " " + goal->target + " " + goal->domain);

    if (goalTokens.empty() || objectTokens.empty()) {
        return false;
    }

    for (const std::string& token : goalTokens) {
        if (TokenSetContains(objectTokens, token)) {
            return true;
        }
    }

    return false;
}

bool IsConflictingAction(const Action& left, const Action& right) {
    if (ToAsciiLower(left.intentAction) == ToAsciiLower(right.intentAction)) {
        return true;
    }

    const std::string leftName = ToAsciiLower(left.name);
    const std::string rightName = ToAsciiLower(right.name);

    if ((leftName == "move_left" && rightName == "move_right") ||
        (leftName == "move_right" && rightName == "move_left") ||
        (leftName == "move_up" && rightName == "move_down") ||
        (leftName == "move_down" && rightName == "move_up")) {
        return true;
    }

    return false;
}

std::int64_t EpochMs(std::chrono::system_clock::time_point value) {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count());
}

std::string SanitizeField(std::string value) {
    std::replace(value.begin(), value.end(), '\t', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    return value;
}

std::vector<std::string> Split(const std::string& value, char delimiter) {
    std::vector<std::string> pieces;
    std::string token;

    for (const char ch : value) {
        if (ch == delimiter) {
            pieces.push_back(token);
            token.clear();
            continue;
        }
        token.push_back(ch);
    }
    pieces.push_back(token);
    return pieces;
}

std::string Join(const std::vector<std::string>& values, char delimiter) {
    std::ostringstream joined;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            joined << delimiter;
        }
        joined << values[index];
    }
    return joined.str();
}

float SkillSuccessRate(const Skill& skill) {
    if (skill.attempts == 0U) {
        return 0.0F;
    }
    return static_cast<float>(skill.success_count) / static_cast<float>(skill.attempts);
}

bool TokenOverlaps(const std::vector<std::string>& lhs, const std::vector<std::string>& rhs) {
    for (const std::string& token : lhs) {
        if (std::binary_search(rhs.begin(), rhs.end(), token)) {
            return true;
        }
    }
    return false;
}

std::string BuildStrategyId(const ReflexGoal* goal) {
    if (goal == nullptr || goal->goal.empty()) {
        return "strategy_inactive";
    }

    std::string normalized = ToAsciiLower(goal->goal);
    for (char& ch : normalized) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) {
            ch = '_';
        }
    }

    if (normalized.size() > 40U) {
        normalized.resize(40U);
    }

    return "strategy_" + normalized;
}

}  // namespace

ContinuousController::ContinuousController(float smoothingAlpha, float sensitivity)
    : smoothingAlpha_(std::clamp(smoothingAlpha, 0.05F, 1.0F)),
      sensitivity_(std::clamp(sensitivity, 0.1F, 2.0F)) {}

ContinuousAction ContinuousController::Apply(const ContinuousAction& target) {
    ContinuousAction adjusted = target;
    adjusted.move_x = ClampUnit(adjusted.move_x * sensitivity_);
    adjusted.move_y = ClampUnit(adjusted.move_y * sensitivity_);
    adjusted.aim_dx = ClampUnit(adjusted.aim_dx * sensitivity_);
    adjusted.aim_dy = ClampUnit(adjusted.aim_dy * sensitivity_);
    adjusted.look_dx = ClampUnit(adjusted.look_dx * sensitivity_);
    adjusted.look_dy = ClampUnit(adjusted.look_dy * sensitivity_);

    if (!hasPrevious_) {
        previous_ = adjusted;
        hasPrevious_ = true;
        return adjusted;
    }

    auto smooth = [this](float previousValue, float nextValue) {
        return ClampUnit(previousValue + ((nextValue - previousValue) * smoothingAlpha_));
    };

    ContinuousAction output;
    output.move_x = smooth(previous_.move_x, adjusted.move_x);
    output.move_y = smooth(previous_.move_y, adjusted.move_y);
    output.aim_dx = smooth(previous_.aim_dx, adjusted.aim_dx);
    output.aim_dy = smooth(previous_.aim_dy, adjusted.aim_dy);
    output.look_dx = smooth(previous_.look_dx, adjusted.look_dx);
    output.look_dy = smooth(previous_.look_dy, adjusted.look_dy);
    output.fire = adjusted.fire;
    output.interact = adjusted.interact;

    previous_ = output;
    return output;
}

void ContinuousController::Reset() {
    previous_ = ContinuousAction{};
    hasPrevious_ = false;
}

float ContinuousController::ClampUnit(float value) {
    return iee::ClampUnit(value);
}

CoordinatedOutput ActionCoordinator::resolve(const std::vector<ReflexBundle>& bundles) const {
    CoordinatedOutput output;

    std::vector<ReflexBundle> orderedBundles = bundles;
    std::sort(orderedBundles.begin(), orderedBundles.end(), [](const ReflexBundle& left, const ReflexBundle& right) {
        if (std::abs(left.priority - right.priority) > 0.0001F) {
            return left.priority > right.priority;
        }
        if (left.source != right.source) {
            return left.source < right.source;
        }
        return left.target_object_id < right.target_object_id;
    });

    float weightSum = 0.0F;
    ContinuousAction weighted{};

    for (const ReflexBundle& bundle : orderedBundles) {
        const float weight = ClampPriority(bundle.priority);
        for (const ContinuousAction& action : bundle.continuous_actions) {
            weighted.move_x += action.move_x * weight;
            weighted.move_y += action.move_y * weight;
            weighted.aim_dx += action.aim_dx * weight;
            weighted.aim_dy += action.aim_dy * weight;
            weighted.look_dx += action.look_dx * weight;
            weighted.look_dy += action.look_dy * weight;
            output.continuous.fire = output.continuous.fire || action.fire;
            output.continuous.interact = output.continuous.interact || action.interact;
            weightSum += weight;
        }

        for (const Action& action : bundle.discrete_actions) {
            if (action.intentAction.empty()) {
                continue;
            }

            bool conflict = false;
            for (const Action& existing : output.discrete) {
                if (IsConflictingAction(action, existing)) {
                    conflict = true;
                    break;
                }
            }

            if (!conflict) {
                output.discrete.push_back(action);
            }
        }
    }

    if (weightSum > 0.0001F) {
        output.continuous.move_x = ClampUnit(weighted.move_x / weightSum);
        output.continuous.move_y = ClampUnit(weighted.move_y / weightSum);
        output.continuous.aim_dx = ClampUnit(weighted.aim_dx / weightSum);
        output.continuous.aim_dy = ClampUnit(weighted.aim_dy / weightSum);
        output.continuous.look_dx = ClampUnit(weighted.look_dx / weightSum);
        output.continuous.look_dy = ClampUnit(weighted.look_dy / weightSum);
    }

    return output;
}

ReflexBundle MovementAgent::Propose(
    const WorldModel& model,
    const AttentionMap& attention,
    const ReflexGoal* goal,
    const ReflexSafetyPolicy& safety) const {
    ReflexBundle bundle;
    bundle.source = "movement";
    bundle.priority = IsMeaningfulGoal(goal) ? 0.70F : 0.55F;

    const std::optional<WorldObject> selected = SelectAttentionObject(model, attention);
    if (!selected.has_value()) {
        return bundle;
    }

    bundle.target_object_id = selected->id;

    ContinuousAction action;
    const Vec2 direction = NormalizedDirectionToObject(model, *selected);
    action.move_x = direction.x;
    action.move_y = direction.y;
    action.look_dx = direction.x * 0.35F;
    action.look_dy = direction.y * 0.35F;
    bundle.continuous_actions.push_back(action);

    if (safety.allowExecute) {
        bundle.discrete_actions.push_back(BuildAction("move_toward_target", "move"));
    }

    if (GoalTouchesObject(goal, *selected)) {
        bundle.priority = ClampPriority(bundle.priority + 0.14F);
    }

    return bundle;
}

ReflexBundle AimAgent::Propose(
    const WorldModel& model,
    const AttentionMap& attention,
    const ReflexGoal* goal,
    const ReflexSafetyPolicy& safety) const {
    ReflexBundle bundle;
    bundle.source = "aim";
    bundle.priority = IsMeaningfulGoal(goal) ? 0.64F : 0.52F;

    const std::optional<WorldObject> selected = SelectAttentionObject(model, attention);
    if (!selected.has_value()) {
        return bundle;
    }

    bundle.target_object_id = selected->id;

    ContinuousAction action;
    const Vec2 direction = NormalizedDirectionToObject(model, *selected);
    action.aim_dx = direction.x;
    action.aim_dy = direction.y;
    action.look_dx = direction.x * 0.20F;
    action.look_dy = direction.y * 0.20F;

    const std::string goalLower = goal == nullptr ? "" : ToAsciiLower(goal->goal);
    if (goalLower.find("shoot") != std::string::npos || goalLower.find("fire") != std::string::npos) {
        action.fire = true;
        bundle.priority = ClampPriority(bundle.priority + 0.18F);
        if (safety.allowExecute) {
            bundle.discrete_actions.push_back(BuildAction("shoot", "activate"));
        }
    } else if (safety.allowExecute) {
        bundle.discrete_actions.push_back(BuildAction("aim_target", "select"));
    }

    bundle.continuous_actions.push_back(action);
    return bundle;
}

ReflexBundle InteractionAgent::Propose(
    const WorldModel& model,
    const AttentionMap& attention,
    const ReflexGoal* goal,
    const ReflexSafetyPolicy& safety) const {
    ReflexBundle bundle;
    bundle.source = "interaction";
    bundle.priority = IsMeaningfulGoal(goal) ? 0.72F : 0.60F;

    const std::optional<WorldObject> selected = SelectAttentionObject(model, attention);
    if (!selected.has_value()) {
        return bundle;
    }

    bundle.target_object_id = selected->id;

    ContinuousAction action;
    action.interact = true;
    bundle.continuous_actions.push_back(action);

    if (!safety.allowExecute) {
        return bundle;
    }

    std::string intentAction = "activate";
    std::string name = "activate_target";

    if (goal != nullptr && !goal->preferredActions.empty()) {
        const std::string preferred = ToAsciiLower(goal->preferredActions.front());
        if (preferred == "select" || preferred == "drag" || preferred == "adjust") {
            intentAction = "select";
            name = "select_target";
        } else if (preferred == "set_value" || preferred == "setvalue" || preferred == "input") {
            intentAction = "set_value";
            name = "set_value_target";
        }
    }

    if (GoalTouchesObject(goal, *selected)) {
        bundle.priority = ClampPriority(bundle.priority + 0.16F);
    }

    bundle.discrete_actions.push_back(BuildAction(name, intentAction));
    return bundle;
}

ReflexBundle StrategyAgent::Propose(
    const WorldModel& model,
    const AttentionMap& attention,
    const ReflexGoal* goal,
    const ReflexSafetyPolicy& safety) const {
    ReflexBundle bundle;
    bundle.source = "strategy";
    bundle.priority = IsMeaningfulGoal(goal) ? 0.58F : 0.45F;

    const std::optional<WorldObject> selected = SelectAttentionObject(model, attention);
    if (selected.has_value()) {
        bundle.target_object_id = selected->id;
    }

    ContinuousAction action;
    action.look_dx = selected.has_value() ? NormalizedDirectionToObject(model, *selected).x * 0.15F : 0.25F;
    action.look_dy = selected.has_value() ? NormalizedDirectionToObject(model, *selected).y * 0.15F : -0.10F;
    bundle.continuous_actions.push_back(action);

    if (safety.allowExecute) {
        if (IsMeaningfulGoal(goal)) {
            bundle.discrete_actions.push_back(BuildAction("stabilize_interaction", "select"));
        } else {
            bundle.discrete_actions.push_back(BuildAction("explore_context", "activate"));
        }
    }

    return bundle;
}

std::vector<ReflexBundle> MicroPlanner::refine(const WorldModel&, const ReflexGoal&) const {
    return {};
}

std::vector<ReflexBundle> MicroPlanner::refine(
    const WorldModel&,
    const ReflexGoal& goal,
    const std::vector<ReflexBundle>& bundles) const {
    std::vector<ReflexBundle> refined = bundles;

    for (ReflexBundle& bundle : refined) {
        if (bundle.priority < 0.05F) {
            continue;
        }

        if (goal.active && !goal.goal.empty()) {
            if (bundle.source == "interaction") {
                bundle.priority = ClampPriority(bundle.priority + 0.08F);
            }
            if (!goal.target.empty() && !bundle.target_object_id.empty()) {
                const std::string normalizedTarget = ToAsciiLower(goal.target);
                const std::string normalizedObject = ToAsciiLower(bundle.target_object_id);
                if (normalizedObject.find(normalizedTarget) != std::string::npos) {
                    bundle.priority = ClampPriority(bundle.priority + 0.10F);
                }
            }
        }
    }

    refined.erase(std::remove_if(refined.begin(), refined.end(), [](const ReflexBundle& bundle) {
        return bundle.priority < 0.20F;
    }), refined.end());

    std::sort(refined.begin(), refined.end(), [](const ReflexBundle& left, const ReflexBundle& right) {
        if (std::abs(left.priority - right.priority) > 0.0001F) {
            return left.priority > right.priority;
        }
        if (left.source != right.source) {
            return left.source < right.source;
        }
        return left.target_object_id < right.target_object_id;
    });

    if (refined.size() > 6U) {
        refined.resize(6U);
    }

    return refined;
}

SkillMemoryStore::SkillMemoryStore(std::filesystem::path filePath)
    : filePath_(std::move(filePath)) {
    if (filePath_.empty()) {
        filePath_ = std::filesystem::path("artifacts") / "reflex" / "skills_v3_2.tsv";
    }
}

bool SkillMemoryStore::Load() {
    std::lock_guard<std::mutex> lock(skillsMutex_);
    skills_.clear();

    std::error_code error;
    if (!std::filesystem::exists(filePath_, error)) {
        return true;
    }

    std::ifstream stream(filePath_);
    if (!stream.good()) {
        return false;
    }

    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = Split(line, '\t');
        if (fields.size() < 5U) {
            continue;
        }

        Skill skill;
        skill.name = fields[0];

        const auto parseU64 = [](const std::string& value, std::uint64_t* output) {
            std::uint64_t parsed = 0;
            const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc() || ptr != value.data() + value.size()) {
                return false;
            }
            *output = parsed;
            return true;
        };

        const auto parseI64 = [](const std::string& value, std::int64_t* output) {
            std::int64_t parsed = 0;
            const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc() || ptr != value.data() + value.size()) {
                return false;
            }
            *output = parsed;
            return true;
        };

        parseU64(fields[1], &skill.attempts);
        parseU64(fields[2], &skill.success_count);
        parseI64(fields[3], &skill.updated_at_ms);

        const std::vector<std::string> actionTokens = Split(fields[4], ',');
        for (const std::string& token : actionTokens) {
            if (token.empty()) {
                continue;
            }
            const std::vector<std::string> nameAndIntent = Split(token, ':');
            if (nameAndIntent.empty()) {
                continue;
            }

            Action action;
            action.name = nameAndIntent[0];
            action.intentAction = nameAndIntent.size() > 1U ? nameAndIntent[1] : "activate";
            skill.sequence.push_back(std::move(action));
        }

        if (fields.size() > 5U && !fields[5].empty()) {
            skill.category = fields[5];
        }

        if (fields.size() > 6U && !fields[6].empty()) {
            std::int64_t complexity = 0;
            if (parseI64(fields[6], &complexity)) {
                skill.complexity_level = static_cast<int>(std::clamp<std::int64_t>(complexity, 0, 8));
            }
        }

        if (fields.size() > 7U && !fields[7].empty()) {
            std::int64_t estimatedFrames = 0;
            if (parseI64(fields[7], &estimatedFrames)) {
                skill.estimated_frames = static_cast<int>(std::clamp<std::int64_t>(estimatedFrames, 1, 2000));
            }
        }

        if (fields.size() > 8U && !fields[8].empty()) {
            const std::vector<std::string> dependencies = Split(fields[8], ',');
            for (const std::string& dependency : dependencies) {
                if (!dependency.empty()) {
                    skill.dependencies.push_back(dependency);
                }
            }
            std::sort(skill.dependencies.begin(), skill.dependencies.end());
            skill.dependencies.erase(
                std::unique(skill.dependencies.begin(), skill.dependencies.end()),
                skill.dependencies.end());
        }

        if (skill.category.empty()) {
            skill.category = "primitive";
        }

        if (skill.estimated_frames <= 0) {
            skill.estimated_frames = static_cast<int>((std::max)(std::size_t{1U}, skill.sequence.size()) * 4U);
        }

        if (skill.complexity_level <= 0) {
            if (skill.sequence.size() >= 6U) {
                skill.complexity_level = 2;
            } else if (skill.sequence.size() >= 3U) {
                skill.complexity_level = 1;
            }
        }

        skills_[skill.name] = std::move(skill);
    }

    return true;
}

bool SkillMemoryStore::Save() const {
    std::lock_guard<std::mutex> lock(skillsMutex_);
    std::error_code directoryError;
    std::filesystem::create_directories(filePath_.parent_path(), directoryError);

    std::ofstream stream(filePath_, std::ios::trunc);
    if (!stream.good()) {
        return false;
    }

    for (const auto& entry : skills_) {
        const Skill& skill = entry.second;
        stream << SanitizeField(skill.name) << '\t'
               << skill.attempts << '\t'
               << skill.success_count << '\t'
               << skill.updated_at_ms << '\t';

        for (std::size_t index = 0; index < skill.sequence.size(); ++index) {
            if (index > 0) {
                stream << ',';
            }
            stream << SanitizeField(skill.sequence[index].name)
                   << ':'
                   << SanitizeField(skill.sequence[index].intentAction);
        }

        stream << '\t' << SanitizeField(skill.category)
               << '\t' << skill.complexity_level
               << '\t' << skill.estimated_frames
               << '\t';

        for (std::size_t index = 0; index < skill.dependencies.size(); ++index) {
            if (index > 0) {
                stream << ',';
            }
            stream << SanitizeField(skill.dependencies[index]);
        }
        stream << '\n';
    }

    return true;
}

void SkillMemoryStore::Record(const std::string& name, const std::vector<Action>& sequence, bool success) {
    if (name.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(skillsMutex_);

    Skill& skill = skills_[name];
    skill.name = name;
    skill.attempts += 1U;
    if (success) {
        skill.success_count += 1U;
    }
    if (!sequence.empty()) {
        skill.sequence = sequence;
    }
    if (skill.category.empty()) {
        skill.category = "primitive";
    }
    if (skill.estimated_frames <= 0 || !sequence.empty()) {
        skill.estimated_frames = static_cast<int>((std::max)(std::size_t{1U}, skill.sequence.size()) * 4U);
    }
    if (sequence.size() >= 6U) {
        skill.complexity_level = 2;
    } else if (sequence.size() >= 3U) {
        skill.complexity_level = 1;
    } else {
        skill.complexity_level = 0;
    }
    skill.updated_at_ms = EpochMs(std::chrono::system_clock::now());
}

std::vector<Skill> SkillMemoryStore::Skills(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(skillsMutex_);
    const std::size_t boundedLimit = (std::max)(static_cast<std::size_t>(1U), limit);

    std::vector<Skill> ordered;
    ordered.reserve(skills_.size());
    for (const auto& entry : skills_) {
        ordered.push_back(entry.second);
    }

    std::sort(ordered.begin(), ordered.end(), [](const Skill& left, const Skill& right) {
        const float leftRate = SkillSuccessRate(left);
        const float rightRate = SkillSuccessRate(right);
        if (std::abs(leftRate - rightRate) > 0.0001F) {
            return leftRate > rightRate;
        }
        if (left.attempts != right.attempts) {
            return left.attempts > right.attempts;
        }
        return left.name < right.name;
    });

    if (ordered.size() > boundedLimit) {
        ordered.resize(boundedLimit);
    }

    return ordered;
}

std::vector<Skill> SkillMemoryStore::RankSkillsForGoal(const ReflexGoal* goal, std::size_t limit) const {
    const std::size_t boundedLimit = (std::max)(std::size_t{1U}, limit);
    std::vector<Skill> candidates = Skills((std::max)(boundedLimit, std::size_t{32U}));
    const std::vector<std::string> goalTokens = goal == nullptr
        ? std::vector<std::string>{}
        : TokenizeLower(goal->goal + " " + goal->target + " " + goal->domain);

    std::vector<std::pair<float, Skill>> scored;
    scored.reserve(candidates.size());

    for (Skill& skill : candidates) {
        float score = SkillSuccessRate(skill);
        score += std::min(0.18F, static_cast<float>(skill.attempts) * 0.015F);
        score -= static_cast<float>(skill.complexity_level) * 0.03F;

        if (skill.estimated_frames > 0) {
            score += std::clamp(8.0F / static_cast<float>(skill.estimated_frames), 0.0F, 0.12F);
        }

        if (!goalTokens.empty()) {
            std::string sequenceText;
            for (const Action& action : skill.sequence) {
                sequenceText += action.name;
                sequenceText.push_back(' ');
                sequenceText += action.intentAction;
                sequenceText.push_back(' ');
            }

            const std::vector<std::string> skillTokens = TokenizeLower(
                skill.name + " " + skill.category + " " + sequenceText + " " + Join(skill.dependencies, ' '));
            if (TokenOverlaps(goalTokens, skillTokens)) {
                score += 0.30F;
            }
        }

        scored.push_back({score, skill});
    }

    std::sort(scored.begin(), scored.end(), [](const auto& left, const auto& right) {
        if (std::abs(left.first - right.first) > 0.0001F) {
            return left.first > right.first;
        }
        return left.second.name < right.second.name;
    });

    std::vector<Skill> ranked;
    ranked.reserve((std::min)(boundedLimit, scored.size()));
    for (std::size_t index = 0; index < scored.size() && ranked.size() < boundedLimit; ++index) {
        ranked.push_back(std::move(scored[index].second));
    }

    return ranked;
}

std::vector<SkillNode> SkillMemoryStore::BuildHierarchy(std::size_t limit) const {
    const std::size_t boundedLimit = (std::max)(std::size_t{1U}, limit);
    const std::vector<Skill> ranked = RankSkillsForGoal(nullptr, (std::max)(std::size_t{8U}, boundedLimit));

    std::vector<SkillNode> primitiveNodes;
    primitiveNodes.reserve(ranked.size());

    for (const Skill& skill : ranked) {
        SkillNode node;
        node.id = "skill_" + ToAsciiLower(skill.name);
        std::replace_if(node.id.begin(), node.id.end(), [](char ch) {
            return !((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_');
        }, '_');
        node.name = skill.name;
        node.category = skill.category.empty() ? "primitive" : skill.category;
        node.children = skill.dependencies;
        std::sort(node.children.begin(), node.children.end());
        node.children.erase(std::unique(node.children.begin(), node.children.end()), node.children.end());

        if (node.children.empty()) {
            for (const Action& action : skill.sequence) {
                if (action.name.empty()) {
                    continue;
                }
                SkillCondition condition;
                condition.field = "action";
                condition.expected = action.name;
                node.conditions.push_back(std::move(condition));
                if (node.conditions.size() >= 3U) {
                    break;
                }
            }
        }

        node.last_outcome.success = skill.success_count > 0U;
        node.last_outcome.confidence = SkillSuccessRate(skill);
        node.last_outcome.note = "attempts=" + std::to_string(skill.attempts);
        primitiveNodes.push_back(std::move(node));
    }

    std::sort(primitiveNodes.begin(), primitiveNodes.end(), [](const SkillNode& left, const SkillNode& right) {
        return left.id < right.id;
    });

    std::vector<SkillNode> hierarchy;
    hierarchy.reserve((std::min)(boundedLimit, primitiveNodes.size() + std::size_t{1U}));

    if (primitiveNodes.size() >= 2U && boundedLimit > 1U) {
        SkillNode strategy;
        strategy.id = "strategy_root";
        strategy.name = "goal_strategy";
        strategy.category = "strategy";
        const std::size_t childLimit = (std::min)(primitiveNodes.size(), std::size_t{3U});
        float confidenceAccumulator = 0.0F;
        for (std::size_t index = 0; index < childLimit; ++index) {
            strategy.children.push_back(primitiveNodes[index].id);
            confidenceAccumulator += primitiveNodes[index].last_outcome.confidence;
        }
        strategy.last_outcome.success = confidenceAccumulator > 0.0F;
        strategy.last_outcome.confidence = confidenceAccumulator / static_cast<float>(childLimit);
        strategy.last_outcome.note = "auto_composed";
        hierarchy.push_back(std::move(strategy));
    }

    for (SkillNode& node : primitiveNodes) {
        if (hierarchy.size() >= boundedLimit) {
            break;
        }
        hierarchy.push_back(std::move(node));
    }

    return hierarchy;
}

AttentionMap BuildAttentionMap(const WorldModel& model, std::size_t maxObjects) {
    AttentionMap attention;
    attention.focus_objects.reserve((std::min)(model.objects.size(), maxObjects));

    std::vector<WorldObject> objects;
    objects.reserve(model.objects.size());
    for (const auto& entry : model.objects) {
        objects.push_back(entry.second);
    }

    std::sort(objects.begin(), objects.end(), [](const WorldObject& left, const WorldObject& right) {
        if (std::abs(left.salience - right.salience) > 0.0001F) {
            return left.salience > right.salience;
        }
        return left.id < right.id;
    });

    const std::size_t limit = (std::min)(objects.size(), maxObjects);
    for (std::size_t index = 0; index < limit; ++index) {
        attention.focus_objects.push_back(objects[index]);
    }

    return attention;
}

std::unordered_map<std::string, Vec2> BuildObjectCenters(const WorldModel& model) {
    std::unordered_map<std::string, Vec2> centers;
    centers.reserve(model.objects.size());
    for (const auto& entry : model.objects) {
        centers[entry.first] = CenterOf(entry.second.bbox);
    }
    return centers;
}

std::vector<PredictedState> BuildPredictedStates(
    const WorldModel& model,
    const std::unordered_map<std::string, Vec2>& previousCenters,
    std::uint64_t horizonFrames) {
    std::vector<PredictedState> predictions;
    predictions.reserve(model.objects.size());

    const float horizon = static_cast<float>((std::max)(std::uint64_t{1U}, horizonFrames));

    std::vector<std::string> ids;
    ids.reserve(model.objects.size());
    for (const auto& entry : model.objects) {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end());

    for (const std::string& id : ids) {
        const WorldObject& object = model.objects.at(id);
        const Vec2 current = CenterOf(object.bbox);

        PredictedState prediction;
        prediction.object_id = id;
        prediction.future_position = current;
        prediction.confidence = object.dynamic ? 0.75F : 0.55F;

        const auto previousIt = previousCenters.find(id);
        if (previousIt != previousCenters.end()) {
            const Vec2 velocity{current.x - previousIt->second.x, current.y - previousIt->second.y};
            prediction.future_position.x = current.x + (velocity.x * horizon);
            prediction.future_position.y = current.y + (velocity.y * horizon);
            prediction.confidence = std::clamp(prediction.confidence + 0.18F, 0.0F, 1.0F);
        } else {
            prediction.confidence = std::clamp(prediction.confidence - 0.20F, 0.0F, 1.0F);
        }

        predictions.push_back(prediction);
    }

    std::sort(predictions.begin(), predictions.end(), [](const PredictedState& left, const PredictedState& right) {
        if (std::abs(left.confidence - right.confidence) > 0.0001F) {
            return left.confidence > right.confidence;
        }
        return left.object_id < right.object_id;
    });

    if (predictions.size() > 16U) {
        predictions.resize(16U);
    }

    return predictions;
}

AnticipationSignal BuildAnticipationSignal(
    const WorldModel& model,
    const AttentionMap& attention,
    const std::vector<PredictedState>& predictions,
    std::uint64_t horizonFrames) {
    AnticipationSignal signal;
    signal.horizon_frames = (std::max)(std::uint64_t{1U}, horizonFrames);

    const std::size_t predictionLimit = (std::min)(predictions.size(), std::size_t{8U});
    signal.events.reserve(predictionLimit);
    for (std::size_t index = 0; index < predictionLimit; ++index) {
        AnticipationEvent event;
        event.object_id = predictions[index].object_id;
        event.future_position = predictions[index].future_position;
        event.confidence = predictions[index].confidence;
        signal.events.push_back(std::move(event));
    }

    for (const WorldObject& object : attention.focus_objects) {
        if (!object.type.empty()) {
            signal.anticipated_affordances.push_back(object.type);
        } else if (!object.id.empty()) {
            signal.anticipated_affordances.push_back(object.id);
        }
    }

    if (signal.anticipated_affordances.empty()) {
        std::vector<std::string> ids;
        ids.reserve(model.objects.size());
        for (const auto& entry : model.objects) {
            ids.push_back(entry.first);
        }
        std::sort(ids.begin(), ids.end());
        const std::size_t idLimit = (std::min)(ids.size(), std::size_t{4U});
        for (std::size_t index = 0; index < idLimit; ++index) {
            signal.anticipated_affordances.push_back(ids[index]);
        }
    }

    std::sort(signal.anticipated_affordances.begin(), signal.anticipated_affordances.end());
    signal.anticipated_affordances.erase(
        std::unique(signal.anticipated_affordances.begin(), signal.anticipated_affordances.end()),
        signal.anticipated_affordances.end());

    float topConfidence = 0.0F;
    if (!signal.events.empty()) {
        topConfidence = signal.events.front().confidence;
    }

    const float attentionWeight = attention.focus_objects.empty() ? 0.55F : 0.75F;
    signal.drift_confidence = std::clamp(topConfidence * attentionWeight, 0.0F, 1.0F);
    signal.actionable = signal.drift_confidence >= 0.62F && !signal.events.empty();
    signal.reason = signal.actionable ? "high_confidence_future_state" : "monitor_only";

    return signal;
}

TemporalStrategyPlan BuildTemporalStrategy(
    const ReflexGoal* goal,
    const std::vector<Skill>& rankedSkills,
    const AttentionMap& attention,
    const AnticipationSignal& anticipation) {
    TemporalStrategyPlan strategy;
    strategy.strategy_id = BuildStrategyId(goal);
    strategy.goal = goal == nullptr ? "" : goal->goal;

    if (goal == nullptr || !goal->active || goal->goal.empty()) {
        return strategy;
    }

    strategy.active = true;

    const std::string targetObjectId = !attention.focus_objects.empty()
        ? attention.focus_objects.front().id
        : (anticipation.events.empty() ? std::string{} : anticipation.events.front().object_id);

    float confidenceAccumulator = 0.0F;
    const std::size_t milestoneLimit = (std::min)(rankedSkills.size(), std::size_t{3U});
    for (std::size_t index = 0; index < milestoneLimit; ++index) {
        StrategyMilestone milestone;
        milestone.skill_name = rankedSkills[index].name;
        milestone.target_object_id = targetObjectId;
        milestone.completed = false;
        strategy.milestones.push_back(std::move(milestone));
        confidenceAccumulator += SkillSuccessRate(rankedSkills[index]);
    }

    if (strategy.milestones.empty()) {
        StrategyMilestone fallbackMilestone;
        fallbackMilestone.skill_name = goal->preferredActions.empty()
            ? "goal_progress"
            : goal->preferredActions.front();
        fallbackMilestone.target_object_id = goal->target;
        strategy.milestones.push_back(std::move(fallbackMilestone));
        confidenceAccumulator = 0.45F;
    }

    strategy.confidence = std::clamp(
        (confidenceAccumulator / static_cast<float>(strategy.milestones.size())) * 0.75F +
            (anticipation.drift_confidence * 0.25F),
        0.0F,
        1.0F);

    strategy.horizon_frames = static_cast<std::uint64_t>((std::max)(
        std::size_t{6U},
        strategy.milestones.size() * 4U + static_cast<std::size_t>(anticipation.horizon_frames)));

    return strategy;
}

PreemptionDecision EvaluatePreemption(
    const TemporalStrategyPlan& strategy,
    const AnticipationSignal& anticipation,
    const ReflexDecision& decision,
    const std::vector<ReflexBundle>& bundles) {
    PreemptionDecision preemption;

    if (!strategy.active) {
        preemption.reason = "strategy_inactive";
        preemption.confidence = 0.0F;
        return preemption;
    }

    if (bundles.empty()) {
        preemption.should_preempt = true;
        preemption.reason = "no_bundle_candidates";
        preemption.suggested_source = "meta_policy";
        preemption.confidence = 0.85F;
        return preemption;
    }

    const ReflexBundle* highest = &bundles.front();
    for (const ReflexBundle& bundle : bundles) {
        if (bundle.priority > highest->priority + 0.0001F) {
            highest = &bundle;
        } else if (std::abs(bundle.priority - highest->priority) <= 0.0001F && bundle.source < highest->source) {
            highest = &bundle;
        }
    }

    const float priorityDelta = std::clamp(highest->priority - decision.priority, 0.0F, 1.0F);
    const float anticipationBoost = anticipation.actionable ? (anticipation.drift_confidence * 0.35F) : 0.0F;
    preemption.confidence = std::clamp(priorityDelta + anticipationBoost, 0.0F, 1.0F);

    if (preemption.confidence >= 0.45F && highest->priority >= 0.70F) {
        preemption.should_preempt = true;
        preemption.reason = anticipation.actionable ? "anticipation_override" : "higher_priority_bundle";
        preemption.suggested_source = highest->source;
    } else {
        preemption.reason = "keep_current_plan";
        preemption.suggested_source = highest->source;
    }

    return preemption;
}

std::string SerializeContinuousActionJson(const ContinuousAction& action) {
    std::ostringstream json;
    json << "{";
    json << "\"move_x\":" << action.move_x << ",";
    json << "\"move_y\":" << action.move_y << ",";
    json << "\"aim_dx\":" << action.aim_dx << ",";
    json << "\"aim_dy\":" << action.aim_dy << ",";
    json << "\"look_dx\":" << action.look_dx << ",";
    json << "\"look_dy\":" << action.look_dy << ",";
    json << "\"fire\":" << (action.fire ? "true" : "false") << ",";
    json << "\"interact\":" << (action.interact ? "true" : "false");
    json << "}";
    return json.str();
}

std::string SerializeReflexBundleJson(const ReflexBundle& bundle) {
    std::ostringstream json;
    json << "{";
    json << "\"source\":\"" << EscapeJson(bundle.source) << "\",";
    json << "\"target_object_id\":\"" << EscapeJson(bundle.target_object_id) << "\",";
    json << "\"priority\":" << bundle.priority << ",";

    json << "\"discrete_actions\":[";
    for (std::size_t index = 0; index < bundle.discrete_actions.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        const Action& action = bundle.discrete_actions[index];
        json << "{";
        json << "\"name\":\"" << EscapeJson(action.name) << "\",";
        json << "\"intent_action\":\"" << EscapeJson(action.intentAction) << "\"";
        json << "}";
    }
    json << "],";

    json << "\"continuous_actions\":[";
    for (std::size_t index = 0; index < bundle.continuous_actions.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << SerializeContinuousActionJson(bundle.continuous_actions[index]);
    }
    json << "]";
    json << "}";
    return json.str();
}

std::string SerializeReflexBundlesJson(const std::vector<ReflexBundle>& bundles) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < bundles.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << SerializeReflexBundleJson(bundles[index]);
    }
    json << "]";
    return json.str();
}

std::string SerializeCoordinatedOutputJson(const CoordinatedOutput& output) {
    std::ostringstream json;
    json << "{";
    json << "\"continuous\":" << SerializeContinuousActionJson(output.continuous) << ",";
    json << "\"discrete\":[";
    for (std::size_t index = 0; index < output.discrete.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        const Action& action = output.discrete[index];
        json << "{";
        json << "\"name\":\"" << EscapeJson(action.name) << "\",";
        json << "\"intent_action\":\"" << EscapeJson(action.intentAction) << "\"";
        json << "}";
    }
    json << "]";
    json << "}";
    return json.str();
}

std::string SerializeAttentionMapJson(const AttentionMap& attention) {
    std::ostringstream json;
    json << "{";
    json << "\"focus_objects\":[";
    for (std::size_t index = 0; index < attention.focus_objects.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        const WorldObject& object = attention.focus_objects[index];
        json << "{";
        json << "\"id\":\"" << EscapeJson(object.id) << "\",";
        json << "\"type\":\"" << EscapeJson(object.type) << "\",";
        json << "\"label\":\"" << EscapeJson(object.label) << "\",";
        json << "\"salience\":" << object.salience;
        json << "}";
    }
    json << "]";
    json << "}";
    return json.str();
}

std::string SerializePredictedStatesJson(const std::vector<PredictedState>& predictions) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < predictions.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        const PredictedState& prediction = predictions[index];
        json << "{";
        json << "\"object_id\":\"" << EscapeJson(prediction.object_id) << "\",";
        json << "\"future_position\":{";
        json << "\"x\":" << prediction.future_position.x << ",";
        json << "\"y\":" << prediction.future_position.y;
        json << "},";
        json << "\"confidence\":" << prediction.confidence;
        json << "}";
    }
    json << "]";
    return json.str();
}

std::string SerializeSkillJson(const Skill& skill) {
    std::ostringstream json;
    json << "{";
    json << "\"name\":\"" << EscapeJson(skill.name) << "\",";
    json << "\"attempts\":" << skill.attempts << ",";
    json << "\"success_count\":" << skill.success_count << ",";
    json << "\"updated_at_ms\":" << skill.updated_at_ms << ",";
    json << "\"category\":\"" << EscapeJson(skill.category) << "\",";
    json << "\"complexity_level\":" << skill.complexity_level << ",";
    json << "\"estimated_frames\":" << skill.estimated_frames << ",";
    json << "\"dependencies\":[";
    for (std::size_t index = 0; index < skill.dependencies.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "\"" << EscapeJson(skill.dependencies[index]) << "\"";
    }
    json << "],";
    json << "\"sequence\":[";
    for (std::size_t index = 0; index < skill.sequence.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "{";
        json << "\"name\":\"" << EscapeJson(skill.sequence[index].name) << "\",";
        json << "\"intent_action\":\"" << EscapeJson(skill.sequence[index].intentAction) << "\"";
        json << "}";
    }
    json << "]";
    json << "}";
    return json.str();
}

std::string SerializeSkillsJson(const std::vector<Skill>& skills) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < skills.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << SerializeSkillJson(skills[index]);
    }
    json << "]";
    return json.str();
}

std::string SerializeSkillNodeJson(const SkillNode& node) {
    std::ostringstream json;
    json << "{";
    json << "\"id\":\"" << EscapeJson(node.id) << "\",";
    json << "\"name\":\"" << EscapeJson(node.name) << "\",";
    json << "\"category\":\"" << EscapeJson(node.category) << "\",";
    json << "\"children\":[";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "\"" << EscapeJson(node.children[index]) << "\"";
    }
    json << "],";
    json << "\"conditions\":[";
    for (std::size_t index = 0; index < node.conditions.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "{";
        json << "\"field\":\"" << EscapeJson(node.conditions[index].field) << "\",";
        json << "\"expected\":\"" << EscapeJson(node.conditions[index].expected) << "\"";
        json << "}";
    }
    json << "],";
    json << "\"last_outcome\":{";
    json << "\"success\":" << (node.last_outcome.success ? "true" : "false") << ",";
    json << "\"confidence\":" << node.last_outcome.confidence << ",";
    json << "\"note\":\"" << EscapeJson(node.last_outcome.note) << "\"";
    json << "}";
    json << "}";
    return json.str();
}

std::string SerializeSkillNodesJson(const std::vector<SkillNode>& nodes) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << SerializeSkillNodeJson(nodes[index]);
    }
    json << "]";
    return json.str();
}

std::string SerializeAnticipationSignalJson(const AnticipationSignal& signal) {
    std::ostringstream json;
    json << "{";
    json << "\"horizon_frames\":" << signal.horizon_frames << ",";
    json << "\"events\":[";
    for (std::size_t index = 0; index < signal.events.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        const AnticipationEvent& event = signal.events[index];
        json << "{";
        json << "\"object_id\":\"" << EscapeJson(event.object_id) << "\",";
        json << "\"future_position\":{";
        json << "\"x\":" << event.future_position.x << ",";
        json << "\"y\":" << event.future_position.y;
        json << "},";
        json << "\"confidence\":" << event.confidence;
        json << "}";
    }
    json << "],";
    json << "\"anticipated_affordances\":[";
    for (std::size_t index = 0; index < signal.anticipated_affordances.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "\"" << EscapeJson(signal.anticipated_affordances[index]) << "\"";
    }
    json << "],";
    json << "\"drift_confidence\":" << signal.drift_confidence << ",";
    json << "\"actionable\":" << (signal.actionable ? "true" : "false") << ",";
    json << "\"reason\":\"" << EscapeJson(signal.reason) << "\"";
    json << "}";
    return json.str();
}

std::string SerializeTemporalStrategyJson(const TemporalStrategyPlan& strategy) {
    std::ostringstream json;
    json << "{";
    json << "\"strategy_id\":\"" << EscapeJson(strategy.strategy_id) << "\",";
    json << "\"goal\":\"" << EscapeJson(strategy.goal) << "\",";
    json << "\"active\":" << (strategy.active ? "true" : "false") << ",";
    json << "\"confidence\":" << strategy.confidence << ",";
    json << "\"horizon_frames\":" << strategy.horizon_frames << ",";
    json << "\"milestones\":[";
    for (std::size_t index = 0; index < strategy.milestones.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        const StrategyMilestone& milestone = strategy.milestones[index];
        json << "{";
        json << "\"skill_name\":\"" << EscapeJson(milestone.skill_name) << "\",";
        json << "\"target_object_id\":\"" << EscapeJson(milestone.target_object_id) << "\",";
        json << "\"completed\":" << (milestone.completed ? "true" : "false");
        json << "}";
    }
    json << "]";
    json << "}";
    return json.str();
}

std::string SerializePreemptionDecisionJson(const PreemptionDecision& decision) {
    std::ostringstream json;
    json << "{";
    json << "\"should_preempt\":" << (decision.should_preempt ? "true" : "false") << ",";
    json << "\"reason\":\"" << EscapeJson(decision.reason) << "\",";
    json << "\"suggested_source\":\"" << EscapeJson(decision.suggested_source) << "\",";
    json << "\"confidence\":" << decision.confidence;
    json << "}";
    return json.str();
}

}  // namespace iee
