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

        skills_[skill.name] = std::move(skill);
    }

    return true;
}

bool SkillMemoryStore::Save() const {
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
        stream << '\n';
    }

    return true;
}

void SkillMemoryStore::Record(const std::string& name, const std::vector<Action>& sequence, bool success) {
    if (name.empty()) {
        return;
    }

    Skill& skill = skills_[name];
    skill.name = name;
    skill.attempts += 1U;
    if (success) {
        skill.success_count += 1U;
    }
    if (!sequence.empty()) {
        skill.sequence = sequence;
    }
    skill.updated_at_ms = EpochMs(std::chrono::system_clock::now());
}

std::vector<Skill> SkillMemoryStore::Skills(std::size_t limit) const {
    const std::size_t boundedLimit = (std::max)(static_cast<std::size_t>(1U), limit);

    std::vector<Skill> ordered;
    ordered.reserve(skills_.size());
    for (const auto& entry : skills_) {
        ordered.push_back(entry.second);
    }

    std::sort(ordered.begin(), ordered.end(), [](const Skill& left, const Skill& right) {
        if (left.success_count != right.success_count) {
            return left.success_count > right.success_count;
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

}  // namespace iee
