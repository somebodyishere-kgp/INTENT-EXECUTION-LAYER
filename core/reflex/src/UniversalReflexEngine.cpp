#include "UniversalReflexEngine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <utility>

#include "InteractionGraph.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

namespace iee {
namespace {

BoundingBox ToBoundingBox(const RECT& rect) {
    BoundingBox bbox;
    bbox.left = rect.left;
    bbox.top = rect.top;
    bbox.right = rect.right;
    bbox.bottom = rect.bottom;
    return bbox;
}

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

std::string Narrow(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (requiredBytes <= 1) {
        return "";
    }

    std::string result(static_cast<std::size_t>(requiredBytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), requiredBytes, nullptr, nullptr);
    result.pop_back();
    return result;
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
    token.reserve(16U);

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

    const std::string normalized = ToAsciiLower(value);
    return std::binary_search(haystack.begin(), haystack.end(), normalized);
}

std::string NormalizeActionToken(const std::string& action) {
    const std::string lower = ToAsciiLower(action);
    if (lower == "activate" || lower == "click" || lower == "acquire" ||
        lower == "target" || lower == "track" || lower == "navigate") {
        return "activate";
    }

    if (lower == "select" || lower == "drag" || lower == "adjust") {
        return "select";
    }

    if (lower == "set_value" || lower == "setvalue" || lower == "input" || lower == "type") {
        return "set_value";
    }

    return lower;
}

bool GoalMatchesObject(const ReflexGoal& goal, const WorldObject& object) {
    if (!goal.active) {
        return false;
    }

    const std::vector<std::string> objectTokens = TokenizeLower(object.id + " " + object.type + " " + object.label);
    const std::vector<std::string> goalTokens =
        TokenizeLower(goal.goal + " " + goal.target + " " + goal.domain);

    if (goalTokens.empty() || objectTokens.empty()) {
        return false;
    }

    std::size_t matched = 0;
    for (const std::string& token : goalTokens) {
        if (TokenSetContains(objectTokens, token)) {
            ++matched;
        }
    }

    if (!goal.target.empty()) {
        const std::vector<std::string> targetTokens = TokenizeLower(goal.target);
        bool hasTargetMatch = false;
        for (const std::string& token : targetTokens) {
            if (TokenSetContains(objectTokens, token)) {
                hasTargetMatch = true;
                break;
            }
        }
        if (!hasTargetMatch) {
            return false;
        }
    }

    return matched > 0U;
}

std::string SelectGoalPreferredAction(const ReflexGoal& goal, const std::vector<std::string>& affordanceActions) {
    if (!goal.active || goal.preferredActions.empty() || affordanceActions.empty()) {
        return "";
    }

    for (const std::string& preferred : goal.preferredActions) {
        const std::string normalizedPreferred = NormalizeActionToken(preferred);
        for (const std::string& candidate : affordanceActions) {
            const std::string normalizedCandidate = NormalizeActionToken(candidate);
            if (normalizedCandidate == normalizedPreferred || ToAsciiLower(candidate) == ToAsciiLower(preferred)) {
                return candidate;
            }
        }
    }

    return "";
}

std::size_t HashCombine(std::size_t seed, std::size_t value) {
    return seed ^ (value + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) + (seed << 6U) + (seed >> 2U));
}

double BoxArea(const BoundingBox& bbox) {
    const int width = (std::max)(0, bbox.right - bbox.left);
    const int height = (std::max)(0, bbox.bottom - bbox.top);
    return static_cast<double>(width) * static_cast<double>(height);
}

bool BoxesOverlap(const BoundingBox& left, const BoundingBox& right) {
    return left.left < right.right && left.right > right.left && left.top < right.bottom && left.bottom > right.top;
}

double OverlapRatio(const BoundingBox& left, const BoundingBox& right) {
    if (!BoxesOverlap(left, right)) {
        return 0.0;
    }

    const int overlapLeft = (std::max)(left.left, right.left);
    const int overlapTop = (std::max)(left.top, right.top);
    const int overlapRight = (std::min)(left.right, right.right);
    const int overlapBottom = (std::min)(left.bottom, right.bottom);

    const BoundingBox overlap{overlapLeft, overlapTop, overlapRight, overlapBottom};
    const double overlapArea = BoxArea(overlap);
    const double denominator = (std::max)(1.0, (std::min)(BoxArea(left), BoxArea(right)));
    return std::clamp(overlapArea / denominator, 0.0, 1.0);
}

double CenterDistance(const BoundingBox& left, const BoundingBox& right) {
    const double leftX = static_cast<double>(left.left + left.right) / 2.0;
    const double leftY = static_cast<double>(left.top + left.bottom) / 2.0;
    const double rightX = static_cast<double>(right.left + right.right) / 2.0;
    const double rightY = static_cast<double>(right.top + right.bottom) / 2.0;

    const double dx = leftX - rightX;
    const double dy = leftY - rightY;
    return std::sqrt((dx * dx) + (dy * dy));
}

bool ContainsPoint(const BoundingBox& box, double x, double y) {
    return x >= static_cast<double>(box.left) && x <= static_cast<double>(box.right) &&
        y >= static_cast<double>(box.top) && y <= static_cast<double>(box.bottom);
}

std::string ClassifyNodeType(const InteractionNode& node) {
    const std::string lowerType = ToAsciiLower(node.type);

    if (node.hidden || node.offscreen || node.collapsed) {
        return "obstacle";
    }

    if (lowerType.find("menu") != std::string::npos ||
        lowerType.find("list") != std::string::npos ||
        lowerType.find("tree") != std::string::npos ||
        lowerType.find("tab") != std::string::npos) {
        return "navigation_element";
    }

    if (lowerType.find("pane") != std::string::npos ||
        lowerType.find("window") != std::string::npos ||
        lowerType.find("slider") != std::string::npos ||
        lowerType.find("scroll") != std::string::npos) {
        return "control_surface";
    }

    if (node.executionPlan.executable && node.intentBinding.action != IntentAction::Unknown) {
        return "interactive_object";
    }

    return "target";
}

std::string ClassifyVisualType(const VisualElement& element, int screenWidth, int screenHeight) {
    if (element.textLike) {
        return "target";
    }

    const BoundingBox bbox = ToBoundingBox(element.bounds);
    const double area = BoxArea(bbox);
    const double screenArea = (std::max)(1.0, static_cast<double>(screenWidth) * static_cast<double>(screenHeight));
    const double occupancy = area / screenArea;

    if (element.edgeDensity >= 0.45) {
        return "dynamic_object";
    }

    if (occupancy >= 0.08) {
        return "control_surface";
    }

    return "resource";
}

std::vector<std::string> SortActions(std::vector<std::string> actions) {
    std::sort(actions.begin(), actions.end());
    actions.erase(std::unique(actions.begin(), actions.end()), actions.end());
    return actions;
}

std::vector<std::string> AffordancesForType(const std::string& type) {
    if (type == "interactive_object") {
        return {"activate", "click"};
    }
    if (type == "control_surface") {
        return {"adjust", "drag"};
    }
    if (type == "dynamic_object") {
        return {"target", "track"};
    }
    if (type == "text_region" || type == "target") {
        return {"input", "read"};
    }
    if (type == "navigation_element") {
        return {"click", "navigate"};
    }
    if (type == "resource") {
        return {"acquire", "activate"};
    }
    if (type == "obstacle") {
        return {"avoid"};
    }

    return {"explore"};
}

std::int64_t EpochMs(std::chrono::system_clock::time_point value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
}

std::string BestEffortIntentAction(const std::string& affordanceAction) {
    const std::string lower = ToAsciiLower(affordanceAction);

    if (lower == "activate" || lower == "click" || lower == "acquire" || lower == "target" || lower == "track") {
        return "activate";
    }
    if (lower == "navigate") {
        return "activate";
    }
    if (lower == "drag" || lower == "adjust") {
        return "select";
    }

    return "";
}

void TrimSamples(std::deque<double>* values, std::size_t maxSamples) {
    while (values->size() > maxSamples) {
        values->pop_front();
    }
}

double Mean(const std::deque<double>& values) {
    if (values.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

double Percentile95(std::deque<double> values) {
    if (values.empty()) {
        return 0.0;
    }

    std::sort(values.begin(), values.end());
    const std::size_t index = ((values.size() - 1U) * 95U) / 100U;
    return values[index];
}

}  // namespace

std::vector<UniversalFeature> UniversalFeatureExtractor::Extract(const EnvironmentState& state, const POINT* previousCursor) {
    std::vector<UniversalFeature> features;

    std::vector<std::string> nodeIds;
    nodeIds.reserve(state.unifiedState.interactionGraph.nodes.size());
    for (const auto& entry : state.unifiedState.interactionGraph.nodes) {
        nodeIds.push_back(entry.first);
    }
    std::sort(nodeIds.begin(), nodeIds.end());

    for (const std::string& nodeId : nodeIds) {
        const auto it = state.unifiedState.interactionGraph.nodes.find(nodeId);
        if (it == state.unifiedState.interactionGraph.nodes.end()) {
            continue;
        }

        const InteractionNode& node = it->second;

        UniversalFeature feature;
        feature.id = "uig:" + node.id;
        feature.type = ClassifyNodeType(node);
        feature.bbox = ToBoundingBox(node.bounds);
        feature.actionable = node.executionPlan.executable && node.intentBinding.action != IntentAction::Unknown;
        feature.dynamic = false;
        feature.salience = static_cast<float>(std::clamp((node.confidence * 0.8) + (feature.actionable ? 0.2 : 0.0), 0.0, 1.0));
        feature.label = node.label;
        feature.source = "uig";
        features.push_back(std::move(feature));
    }

    const int screenWidth = (std::max)(1, state.screenState.width);
    const int screenHeight = (std::max)(1, state.screenState.height);

    std::vector<VisualElement> visualElements = state.screenState.visualElements;
    std::sort(visualElements.begin(), visualElements.end(), [](const VisualElement& left, const VisualElement& right) {
        return left.id < right.id;
    });

    for (const VisualElement& element : visualElements) {
        UniversalFeature feature;
        feature.id = "visual:" + element.id;
        feature.type = ClassifyVisualType(element, screenWidth, screenHeight);
        feature.bbox = ToBoundingBox(element.bounds);
        feature.actionable = feature.type != "obstacle";
        feature.dynamic = feature.type == "dynamic_object";
        feature.salience = static_cast<float>(std::clamp((element.confidence * 0.6) + (element.edgeDensity * 0.4), 0.0, 1.0));
        feature.label = element.kind;
        feature.source = "screen_visual";
        features.push_back(std::move(feature));
    }

    std::vector<ScreenElement> screenElements = state.screenState.elements;
    std::sort(screenElements.begin(), screenElements.end(), [](const ScreenElement& left, const ScreenElement& right) {
        return left.id < right.id;
    });

    for (const ScreenElement& element : screenElements) {
        UniversalFeature feature;
        feature.id = "screen:" + element.id;
        feature.type = element.textLike ? "text_region" : "interactive_object";
        feature.bbox = ToBoundingBox(element.bounds);
        feature.actionable = true;
        feature.dynamic = false;
        feature.salience = static_cast<float>(std::clamp((element.confidence * 0.75) + (element.focused ? 0.25 : 0.0), 0.0, 1.0));
        feature.label = Narrow(element.label);
        feature.source = "screen_element";
        features.push_back(std::move(feature));
    }

    UniversalFeature cursorFeature;
    cursorFeature.id = "cursor:active";
    cursorFeature.type = "navigation_element";
    cursorFeature.bbox.left = state.cursorPosition.x - 12;
    cursorFeature.bbox.top = state.cursorPosition.y - 12;
    cursorFeature.bbox.right = state.cursorPosition.x + 12;
    cursorFeature.bbox.bottom = state.cursorPosition.y + 12;
    cursorFeature.actionable = false;
    cursorFeature.dynamic = false;
    cursorFeature.salience = 0.25F;
    cursorFeature.label = "cursor_focus";
    cursorFeature.source = "cursor";

    if (previousCursor != nullptr) {
        const int dx = state.cursorPosition.x - previousCursor->x;
        const int dy = state.cursorPosition.y - previousCursor->y;
        const double distance = std::sqrt(static_cast<double>((dx * dx) + (dy * dy)));
        if (distance >= 4.0) {
            cursorFeature.dynamic = true;
            cursorFeature.type = "dynamic_object";
            cursorFeature.salience = static_cast<float>(std::clamp(0.25 + (distance / 64.0), 0.0, 1.0));
        }
    }

    features.push_back(std::move(cursorFeature));

    std::sort(features.begin(), features.end(), [](const UniversalFeature& left, const UniversalFeature& right) {
        return left.id < right.id;
    });

    return features;
}

WorldModel WorldModelBuilder::Build(
    const std::vector<UniversalFeature>& features,
    const WorldModel* previousModel,
    std::uint64_t frameId) const {
    WorldModel model;
    model.frameId = frameId;

    std::size_t signature = static_cast<std::size_t>(0xcbf29ce484222325ULL);

    for (const UniversalFeature& feature : features) {
        WorldObject object;
        object.id = feature.id;
        object.type = feature.type;
        object.bbox = feature.bbox;
        object.label = feature.label;
        object.actionable = feature.actionable;
        object.dynamic = feature.dynamic;
        object.salience = feature.salience;
        object.lastSeenFrame = frameId;

        auto previousIt = previousModel == nullptr
            ? std::unordered_map<std::string, WorldObject>::const_iterator{}
            : previousModel->objects.find(object.id);

        bool changed = true;
        if (previousModel != nullptr && previousIt != previousModel->objects.end()) {
            const WorldObject& previous = previousIt->second;
            const double movement = CenterDistance(previous.bbox, object.bbox);
            object.dynamic = object.dynamic || movement >= 8.0;

            changed = previous.type != object.type ||
                std::abs(previous.salience - object.salience) > 0.05F ||
                movement >= 2.0;
        }

        if (changed) {
            ++model.changedObjects;
        }

        signature = HashCombine(signature, std::hash<std::string>{}(object.id));
        signature = HashCombine(signature, std::hash<std::string>{}(object.type));
        signature = HashCombine(signature, std::hash<int>{}(object.bbox.left));
        signature = HashCombine(signature, std::hash<int>{}(object.bbox.top));
        signature = HashCombine(signature, std::hash<int>{}(object.bbox.right));
        signature = HashCombine(signature, std::hash<int>{}(object.bbox.bottom));

        model.objects[object.id] = std::move(object);
    }

    std::vector<std::string> ids;
    ids.reserve(model.objects.size());
    for (const auto& entry : model.objects) {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end());

    const std::size_t bounded = std::min<std::size_t>(ids.size(), 64U);
    for (std::size_t i = 0; i < bounded; ++i) {
        const WorldObject& left = model.objects[ids[i]];

        if (previousModel != nullptr) {
            const auto previousIt = previousModel->objects.find(left.id);
            if (previousIt != previousModel->objects.end()) {
                const double movement = CenterDistance(previousIt->second.bbox, left.bbox);
                if (movement >= 8.0) {
                    Relationship movementRelation;
                    movementRelation.fromId = left.id;
                    movementRelation.toId = left.id;
                    movementRelation.type = "motion";
                    movementRelation.strength = static_cast<float>(std::clamp(movement / 128.0, 0.0, 1.0));
                    model.relationships.push_back(std::move(movementRelation));
                }
            }
        }

        const double leftCenterX = static_cast<double>(left.bbox.left + left.bbox.right) / 2.0;
        const double leftCenterY = static_cast<double>(left.bbox.top + left.bbox.bottom) / 2.0;

        for (std::size_t j = i + 1U; j < bounded; ++j) {
            const WorldObject& right = model.objects[ids[j]];

            const double overlap = OverlapRatio(left.bbox, right.bbox);
            if (overlap > 0.0) {
                Relationship relation;
                relation.fromId = left.id;
                relation.toId = right.id;
                relation.type = "overlap";
                relation.strength = static_cast<float>(overlap);
                model.relationships.push_back(std::move(relation));
            }

            const double distance = CenterDistance(left.bbox, right.bbox);
            if (distance <= 180.0) {
                Relationship relation;
                relation.fromId = left.id;
                relation.toId = right.id;
                relation.type = "proximity";
                relation.strength = static_cast<float>(std::clamp(1.0 - (distance / 180.0), 0.0, 1.0));
                model.relationships.push_back(std::move(relation));
            }

            const double rightCenterX = static_cast<double>(right.bbox.left + right.bbox.right) / 2.0;
            const double rightCenterY = static_cast<double>(right.bbox.top + right.bbox.bottom) / 2.0;

            if (ContainsPoint(left.bbox, rightCenterX, rightCenterY) && BoxArea(left.bbox) > BoxArea(right.bbox)) {
                Relationship relation;
                relation.fromId = left.id;
                relation.toId = right.id;
                relation.type = "hierarchy";
                relation.strength = 1.0F;
                model.relationships.push_back(std::move(relation));
            } else if (ContainsPoint(right.bbox, leftCenterX, leftCenterY) && BoxArea(right.bbox) > BoxArea(left.bbox)) {
                Relationship relation;
                relation.fromId = right.id;
                relation.toId = left.id;
                relation.type = "hierarchy";
                relation.strength = 1.0F;
                model.relationships.push_back(std::move(relation));
            }
        }
    }

    std::sort(model.relationships.begin(), model.relationships.end(), [](const Relationship& left, const Relationship& right) {
        if (left.fromId != right.fromId) {
            return left.fromId < right.fromId;
        }
        if (left.toId != right.toId) {
            return left.toId < right.toId;
        }
        if (left.type != right.type) {
            return left.type < right.type;
        }
        return left.strength > right.strength;
    });

    for (const Relationship& relationship : model.relationships) {
        signature = HashCombine(signature, std::hash<std::string>{}(relationship.fromId));
        signature = HashCombine(signature, std::hash<std::string>{}(relationship.toId));
        signature = HashCombine(signature, std::hash<std::string>{}(relationship.type));
    }

    model.signature = static_cast<std::uint64_t>(signature == 0 ? 1 : signature);
    return model;
}

std::vector<Affordance> AffordanceEngine::Infer(const WorldModel& model) {
    std::vector<Affordance> affordances;
    affordances.reserve(model.objects.size());

    std::vector<std::string> ids;
    ids.reserve(model.objects.size());
    for (const auto& entry : model.objects) {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end());

    for (const std::string& id : ids) {
        const auto it = model.objects.find(id);
        if (it == model.objects.end()) {
            continue;
        }

        Affordance affordance;
        affordance.object_id = id;
        affordance.actions = SortActions(AffordancesForType(it->second.type));
        affordance.confidence = static_cast<float>(std::clamp((it->second.salience * 0.8F) + 0.2F, 0.0F, 1.0F));
        affordances.push_back(std::move(affordance));
    }

    return affordances;
}

MetaPolicyEngine::MetaPolicyEngine() {
    rules_.push_back({{"threat", "dynamic_object"}, {"avoid_threat", ""}, 0.95F});
    rules_.push_back({{"target", "target"}, {"move_toward_target", "activate"}, 0.85F});
    rules_.push_back({{"resource", "resource"}, {"acquire_resource", "activate"}, 0.80F});
    rules_.push_back({{"obstacle", "obstacle"}, {"avoid_obstacle", ""}, 0.88F});
    rules_.push_back({{"interactive", "interactive_object"}, {"activate_object", "activate"}, 0.78F});
    rules_.push_back({{"unknown", "unknown"}, {"explore_unknown", "activate"}, 0.55F});
}

ReflexDecision MetaPolicyEngine::Decide(
    const WorldModel& model,
    const std::vector<Affordance>& affordances,
    const ReflexSafetyPolicy& safety,
    const std::unordered_map<std::string, float>& failureBias,
    const ReflexGoal* goal) const {
    ReflexDecision best;

    std::unordered_map<std::string, Affordance> affordanceById;
    affordanceById.reserve(affordances.size());
    for (const Affordance& affordance : affordances) {
        affordanceById[affordance.object_id] = affordance;
    }

    std::vector<std::string> ids;
    ids.reserve(model.objects.size());
    for (const auto& entry : model.objects) {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end());

    for (const std::string& id : ids) {
        const auto objectIt = model.objects.find(id);
        if (objectIt == model.objects.end()) {
            continue;
        }

        const WorldObject& object = objectIt->second;
        const auto affordanceIt = affordanceById.find(id);
        if (affordanceIt == affordanceById.end() || affordanceIt->second.actions.empty()) {
            continue;
        }

        float basePriority = 0.55F;
        std::string reason = "unknown_explore";

        if (object.type == "dynamic_object" && object.salience >= 0.75F) {
            basePriority = 0.95F;
            reason = "threat_reduce_risk";
        } else if (object.type == "target") {
            basePriority = 0.85F;
            reason = "target_move_toward";
        } else if (object.type == "resource") {
            basePriority = 0.80F;
            reason = "resource_acquire";
        } else if (object.type == "obstacle") {
            basePriority = 0.88F;
            reason = "obstacle_avoid";
        } else if (object.type == "interactive_object") {
            basePriority = 0.78F;
            reason = "interactive_activate";
        }

        std::string selectedAction = affordanceIt->second.actions.front();
        if (object.type == "dynamic_object" && object.salience >= 0.75F) {
            selectedAction = "avoid";
        }

        const bool goalMatched = goal != nullptr && GoalMatchesObject(*goal, object);
        if (goalMatched) {
            const std::string preferredAction = SelectGoalPreferredAction(*goal, affordanceIt->second.actions);
            if (!preferredAction.empty()) {
                selectedAction = preferredAction;
            }
            basePriority = std::clamp(basePriority + 0.18F, 0.0F, 1.0F);
            reason = "goal_conditioned_" + reason;
        }

        const std::string biasKey = selectedAction + "|" + object.id;
        const auto biasIt = failureBias.find(biasKey);
        if (biasIt != failureBias.end()) {
            basePriority = std::clamp(basePriority - biasIt->second, 0.0F, 1.0F);
        }

        const std::string intentAction = BestEffortIntentAction(selectedAction);
        const bool executable = safety.allowExecute && !intentAction.empty() &&
            object.type != "obstacle" &&
            !(selectedAction == "input" && object.label.empty());

        if (basePriority > best.priority || (std::abs(basePriority - best.priority) <= 0.0001F && object.id < best.objectId)) {
            best.objectId = object.id;
            best.objectType = object.type;
            best.action = intentAction;
            best.targetLabel = object.label.empty() ? object.id : object.label;
            best.reason = reason;
            best.priority = basePriority;
            best.executable = executable;
            best.exploratory = false;
        }
    }

    if (best.objectId.empty() && !model.objects.empty()) {
        const std::string fallbackId = std::min_element(
            model.objects.begin(),
            model.objects.end(),
            [](const auto& left, const auto& right) {
                return left.first < right.first;
            })->first;

        const WorldObject& object = model.objects.at(fallbackId);
        best.objectId = object.id;
        best.objectType = object.type;
        best.action = safety.allowExecute ? "activate" : "";
        best.targetLabel = object.label.empty() ? object.id : object.label;
        best.reason = "fallback_explore";
        best.priority = 0.50F;
        best.executable = safety.allowExecute && !best.action.empty();
        best.exploratory = true;
    }

    return best;
}

std::vector<ExplorationResult> ExplorationEngine::Propose(
    const WorldModel&,
    const std::vector<Affordance>& affordances,
    const ReflexSafetyPolicy& safety,
    std::size_t maxResults) {
    std::vector<ExplorationResult> results;
    if (!safety.allowExploration || maxResults == 0U) {
        return results;
    }

    std::vector<Affordance> ranked = affordances;
    std::sort(ranked.begin(), ranked.end(), [](const Affordance& left, const Affordance& right) {
        if (std::abs(left.confidence - right.confidence) > 0.0001F) {
            return left.confidence > right.confidence;
        }
        return left.object_id < right.object_id;
    });

    for (const Affordance& affordance : ranked) {
        if (results.size() >= maxResults) {
            break;
        }

        std::string selectedAction;
        for (const std::string& action : affordance.actions) {
            const std::string intentAction = BestEffortIntentAction(action);
            if (!intentAction.empty()) {
                selectedAction = intentAction;
                break;
            }
        }

        if (selectedAction.empty()) {
            continue;
        }

        ExplorationResult result;
        result.objectId = affordance.object_id;
        result.action = selectedAction;
        result.success = false;
        result.reward = 0.0F;
        results.push_back(std::move(result));
    }

    return results;
}

UniversalReflexAgent::UniversalReflexAgent() = default;

ReflexStepResult UniversalReflexAgent::Step(
    const EnvironmentState& state,
    const ReflexSafetyPolicy& safety,
    std::int64_t decisionBudgetUs,
    const ReflexGoal* goal) {
    const auto loopStart = std::chrono::steady_clock::now();

    ReflexStepResult result;
    result.features = UniversalFeatureExtractor::Extract(state, hasPreviousCursor_ ? &previousCursor_ : nullptr);

    const std::uint64_t frameId = state.unifiedState.frameId == 0 ? state.sequence : state.unifiedState.frameId;
    result.worldModel = worldBuilder_.Build(
        result.features,
        previousWorldModel_.objects.empty() ? nullptr : &previousWorldModel_,
        frameId);

    result.affordances = AffordanceEngine::Infer(result.worldModel);

    for (Affordance& affordance : result.affordances) {
        auto objectIt = result.worldModel.objects.find(affordance.object_id);
        if (objectIt == result.worldModel.objects.end()) {
            continue;
        }

        for (const std::string& action : affordance.actions) {
            const std::string adjustmentKey = objectIt->second.type + "|" + action;
            const auto adjustmentIt = affordanceConfidenceAdjustments_.find(adjustmentKey);
            if (adjustmentIt != affordanceConfidenceAdjustments_.end()) {
                affordance.confidence = std::clamp(affordance.confidence + adjustmentIt->second, 0.0F, 1.0F);
            }
        }

        objectIt->second.affordances = affordance.actions;
    }

    const auto decisionStart = std::chrono::steady_clock::now();
    result.decision = policyEngine_.Decide(result.worldModel, result.affordances, safety, failureBiasByKey_, goal);
    const auto decisionEnd = std::chrono::steady_clock::now();

    result.decisionTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(decisionEnd - decisionStart).count();
    result.decisionWithinBudget = result.decisionTimeUs <= std::max<std::int64_t>(1LL, decisionBudgetUs);

    if (!result.decision.executable || result.decision.action.empty()) {
        result.exploration = ExplorationEngine::Propose(result.worldModel, result.affordances, safety, 2U);
        if (!result.exploration.empty()) {
            result.decision.objectId = result.exploration.front().objectId;
            result.decision.objectType = result.worldModel.objects.count(result.decision.objectId) > 0U
                ? result.worldModel.objects.at(result.decision.objectId).type
                : "unknown";
            result.decision.action = result.exploration.front().action;
            result.decision.targetLabel = result.worldModel.objects.count(result.decision.objectId) > 0U
                ? (result.worldModel.objects.at(result.decision.objectId).label.empty()
                      ? result.decision.objectId
                      : result.worldModel.objects.at(result.decision.objectId).label)
                : result.decision.objectId;
            result.decision.reason = "bounded_exploration";
            result.decision.priority = (std::max)(0.45F, result.decision.priority);
            result.decision.exploratory = true;
            result.decision.executable = safety.allowExecute && !result.decision.action.empty();
        }
    }

    if (!result.decisionWithinBudget) {
        ++overBudgetDecisions_;
    }
    if (result.decision.exploratory) {
        ++exploratoryDecisions_;
    }

    const auto loopEnd = std::chrono::steady_clock::now();
    result.loopTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(loopEnd - loopStart).count();

    ++decisionCount_;
    ++loopCount_;
    decisionDurationsMs_.push_back(static_cast<double>(result.decisionTimeUs) / 1000.0);
    loopDurationsMs_.push_back(static_cast<double>(result.loopTimeUs) / 1000.0);
    TrimSamples(&decisionDurationsMs_, 2048U);
    TrimSamples(&loopDurationsMs_, 2048U);

    previousWorldModel_ = result.worldModel;
    previousCursor_ = state.cursorPosition;
    hasPreviousCursor_ = true;

    return result;
}

void UniversalReflexAgent::RecordExecutionOutcome(const ReflexDecision& decision, bool success, std::optional<float> reward) {
    if (decision.objectId.empty() || decision.action.empty()) {
        return;
    }

    const float effectiveReward = reward.has_value() ? *reward : (success ? 1.0F : -1.0F);

    ExperienceEntry entry;
    entry.state.signature = previousWorldModel_.signature;
    entry.state.objects = previousWorldModel_.objects.size();
    entry.state.relationships = previousWorldModel_.relationships.size();
    entry.action = decision;
    entry.reward = effectiveReward;
    entry.timestampMs = EpochMs(std::chrono::system_clock::now());

    experience_.push_back(std::move(entry));
    while (experience_.size() > 512U) {
        experience_.pop_front();
    }

    const std::string biasKey = BiasKey(decision);
    float& bias = failureBiasByKey_[biasKey];
    if (success) {
        bias = (std::max)(0.0F, bias - 0.10F);
    } else {
        bias = (std::min)(0.90F, bias + 0.25F);
    }

    const std::string adjustmentKey = decision.objectType + "|" + decision.action;
    float& adjustment = affordanceConfidenceAdjustments_[adjustmentKey];
    if (success) {
        adjustment = (std::min)(0.30F, adjustment + 0.02F);
    } else {
        adjustment = (std::max)(-0.30F, adjustment - 0.03F);
    }
}

ReflexMetricsSnapshot UniversalReflexAgent::Metrics() const {
    ReflexMetricsSnapshot snapshot;
    snapshot.decisions = decisionCount_;
    snapshot.loops = loopCount_;
    snapshot.averageDecisionMs = Mean(decisionDurationsMs_);
    snapshot.p95DecisionMs = Percentile95(decisionDurationsMs_);
    snapshot.averageLoopMs = Mean(loopDurationsMs_);
    snapshot.overBudgetDecisions = overBudgetDecisions_;
    snapshot.exploratoryDecisions = exploratoryDecisions_;
    return snapshot;
}

std::vector<ExperienceEntry> UniversalReflexAgent::Experience(std::size_t limit) const {
    const std::size_t boundedLimit = std::max<std::size_t>(1U, limit);
    const std::size_t start = experience_.size() > boundedLimit ? experience_.size() - boundedLimit : 0U;

    std::vector<ExperienceEntry> entries;
    entries.reserve(experience_.size() - start);
    for (std::size_t index = start; index < experience_.size(); ++index) {
        entries.push_back(experience_[index]);
    }
    return entries;
}

std::string UniversalReflexAgent::BiasKey(const ReflexDecision& decision) {
    return decision.action + "|" + decision.objectId;
}

std::string SerializeBoundingBoxJson(const BoundingBox& bbox) {
    std::ostringstream json;
    json << "{";
    json << "\"left\":" << bbox.left << ",";
    json << "\"top\":" << bbox.top << ",";
    json << "\"right\":" << bbox.right << ",";
    json << "\"bottom\":" << bbox.bottom;
    json << "}";
    return json.str();
}

std::string SerializeUniversalFeaturesJson(const std::vector<UniversalFeature>& features) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < features.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const UniversalFeature& feature = features[index];
        json << "{";
        json << "\"id\":\"" << EscapeJson(feature.id) << "\",";
        json << "\"type\":\"" << EscapeJson(feature.type) << "\",";
        json << "\"bbox\":" << SerializeBoundingBoxJson(feature.bbox) << ",";
        json << "\"actionable\":" << (feature.actionable ? "true" : "false") << ",";
        json << "\"dynamic\":" << (feature.dynamic ? "true" : "false") << ",";
        json << "\"salience\":" << feature.salience << ",";
        json << "\"label\":\"" << EscapeJson(feature.label) << "\",";
        json << "\"source\":\"" << EscapeJson(feature.source) << "\"";
        json << "}";
    }
    json << "]";
    return json.str();
}

std::string SerializeWorldModelJson(const WorldModel& worldModel) {
    std::ostringstream json;
    json << "{";
    json << "\"frame_id\":" << worldModel.frameId << ",";
    json << "\"signature\":" << worldModel.signature << ",";
    json << "\"changed_objects\":" << worldModel.changedObjects << ",";
    json << "\"objects\":[";

    std::vector<std::string> ids;
    ids.reserve(worldModel.objects.size());
    for (const auto& entry : worldModel.objects) {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end());

    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const WorldObject& object = worldModel.objects.at(ids[index]);
        json << "{";
        json << "\"id\":\"" << EscapeJson(object.id) << "\",";
        json << "\"type\":\"" << EscapeJson(object.type) << "\",";
        json << "\"bbox\":" << SerializeBoundingBoxJson(object.bbox) << ",";
        json << "\"label\":\"" << EscapeJson(object.label) << "\",";
        json << "\"actionable\":" << (object.actionable ? "true" : "false") << ",";
        json << "\"dynamic\":" << (object.dynamic ? "true" : "false") << ",";
        json << "\"salience\":" << object.salience << ",";
        json << "\"last_seen_frame\":" << object.lastSeenFrame << ",";
        json << "\"affordances\":[";
        for (std::size_t actionIndex = 0; actionIndex < object.affordances.size(); ++actionIndex) {
            if (actionIndex > 0) {
                json << ",";
            }
            json << "\"" << EscapeJson(object.affordances[actionIndex]) << "\"";
        }
        json << "]";
        json << "}";
    }

    json << "],";
    json << "\"relationships\":[";
    for (std::size_t index = 0; index < worldModel.relationships.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const Relationship& relationship = worldModel.relationships[index];
        json << "{";
        json << "\"from\":\"" << EscapeJson(relationship.fromId) << "\",";
        json << "\"to\":\"" << EscapeJson(relationship.toId) << "\",";
        json << "\"type\":\"" << EscapeJson(relationship.type) << "\",";
        json << "\"strength\":" << relationship.strength;
        json << "}";
    }
    json << "]";
    json << "}";

    return json.str();
}

std::string SerializeAffordancesJson(const std::vector<Affordance>& affordances) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < affordances.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const Affordance& affordance = affordances[index];
        json << "{";
        json << "\"object_id\":\"" << EscapeJson(affordance.object_id) << "\",";
        json << "\"confidence\":" << affordance.confidence << ",";
        json << "\"actions\":[";
        for (std::size_t actionIndex = 0; actionIndex < affordance.actions.size(); ++actionIndex) {
            if (actionIndex > 0) {
                json << ",";
            }
            json << "\"" << EscapeJson(affordance.actions[actionIndex]) << "\"";
        }
        json << "]";
        json << "}";
    }
    json << "]";
    return json.str();
}

std::string SerializeReflexDecisionJson(const ReflexDecision& decision) {
    std::ostringstream json;
    json << "{";
    json << "\"object_id\":\"" << EscapeJson(decision.objectId) << "\",";
    json << "\"object_type\":\"" << EscapeJson(decision.objectType) << "\",";
    json << "\"action\":\"" << EscapeJson(decision.action) << "\",";
    json << "\"target\":\"" << EscapeJson(decision.targetLabel) << "\",";
    json << "\"reason\":\"" << EscapeJson(decision.reason) << "\",";
    json << "\"priority\":" << decision.priority << ",";
    json << "\"executable\":" << (decision.executable ? "true" : "false") << ",";
    json << "\"exploratory\":" << (decision.exploratory ? "true" : "false");
    json << "}";
    return json.str();
}

std::string SerializeExplorationJson(const std::vector<ExplorationResult>& exploration) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < exploration.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const ExplorationResult& entry = exploration[index];
        json << "{";
        json << "\"object_id\":\"" << EscapeJson(entry.objectId) << "\",";
        json << "\"action\":\"" << EscapeJson(entry.action) << "\",";
        json << "\"success\":" << (entry.success ? "true" : "false") << ",";
        json << "\"reward\":" << entry.reward;
        json << "}";
    }
    json << "]";
    return json.str();
}

std::string SerializeReflexStepResultJson(const ReflexStepResult& step) {
    std::ostringstream json;
    json << "{";
    json << "\"features\":" << SerializeUniversalFeaturesJson(step.features) << ",";
    json << "\"world_model\":" << SerializeWorldModelJson(step.worldModel) << ",";
    json << "\"affordances\":" << SerializeAffordancesJson(step.affordances) << ",";
    json << "\"decision\":" << SerializeReflexDecisionJson(step.decision) << ",";
    json << "\"exploration\":" << SerializeExplorationJson(step.exploration) << ",";
    json << "\"decision_time_us\":" << step.decisionTimeUs << ",";
    json << "\"loop_time_us\":" << step.loopTimeUs << ",";
    json << "\"decision_within_budget\":" << (step.decisionWithinBudget ? "true" : "false");
    json << "}";
    return json.str();
}

std::string SerializeReflexMetricsJson(const ReflexMetricsSnapshot& metrics) {
    std::ostringstream json;
    json << "{";
    json << "\"decisions\":" << metrics.decisions << ",";
    json << "\"loops\":" << metrics.loops << ",";
    json << "\"average_decision_ms\":" << metrics.averageDecisionMs << ",";
    json << "\"p95_decision_ms\":" << metrics.p95DecisionMs << ",";
    json << "\"average_loop_ms\":" << metrics.averageLoopMs << ",";
    json << "\"over_budget_decisions\":" << metrics.overBudgetDecisions << ",";
    json << "\"exploratory_decisions\":" << metrics.exploratoryDecisions;
    json << "}";
    return json.str();
}

std::string SerializeExperienceEntriesJson(const std::vector<ExperienceEntry>& entries) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const ExperienceEntry& entry = entries[index];
        json << "{";
        json << "\"state\":{";
        json << "\"signature\":" << entry.state.signature << ",";
        json << "\"objects\":" << entry.state.objects << ",";
        json << "\"relationships\":" << entry.state.relationships;
        json << "},";
        json << "\"action\":" << SerializeReflexDecisionJson(entry.action) << ",";
        json << "\"reward\":" << entry.reward << ",";
        json << "\"timestamp_ms\":" << entry.timestampMs;
        json << "}";
    }
    json << "]";
    return json.str();
}

std::string SerializeReflexGoalJson(const ReflexGoal& goal) {
    std::ostringstream json;
    json << "{";
    json << "\"goal\":\"" << EscapeJson(goal.goal) << "\",";
    json << "\"target\":\"" << EscapeJson(goal.target) << "\",";
    json << "\"domain\":\"" << EscapeJson(goal.domain) << "\",";
    json << "\"active\":" << (goal.active ? "true" : "false") << ",";
    json << "\"updated_at_ms\":" << goal.updatedAtMs << ",";
    json << "\"preferred_actions\":[";
    for (std::size_t index = 0; index < goal.preferredActions.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "\"" << EscapeJson(goal.preferredActions[index]) << "\"";
    }
    json << "]";
    json << "}";
    return json.str();
}

}  // namespace iee
