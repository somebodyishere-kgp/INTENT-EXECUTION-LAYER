#include "IntentResolver.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace iee {
namespace {

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(::towlower(ch));
    });
    return value;
}

std::wstring PrimaryLabel(const Intent& intent) {
    if (!intent.target.label.empty()) {
        return intent.target.label;
    }
    if (!intent.target.automationId.empty()) {
        return intent.target.automationId;
    }
    return intent.target.path;
}

}  // namespace

ResolutionResult IntentResolver::Resolve(
    IntentAction action,
    const std::wstring& requestedTarget,
    const std::vector<Intent>& intents,
    const POINT& cursor,
    const std::unordered_map<std::string, std::uint64_t>& recencyByIntentId,
    float ambiguityThreshold) const {
    ResolutionResult result;

    for (const auto& intent : intents) {
        if (intent.action != action) {
            continue;
        }

        IntentMatch match;
        match.intent = intent;
        match.depthScore = DepthScore(intent);
        match.proximityScore = ProximityScore(intent, cursor);
        match.focusScore = FocusScore(intent);
        match.recencyScore = RecencyScore(intent, recencyByIntentId);

        const float labelScore = LabelScore(intent, requestedTarget);
        const float confidenceScore = std::max(0.0F, std::min(1.0F, intent.confidence));

        match.score =
            0.28F * labelScore +
            0.18F * match.depthScore +
            0.18F * match.proximityScore +
            0.18F * match.focusScore +
            0.10F * match.recencyScore +
            0.08F * confidenceScore;

        result.ranked.push_back(std::move(match));
    }

    std::sort(result.ranked.begin(), result.ranked.end(), [](const IntentMatch& left, const IntentMatch& right) {
        if (left.score == right.score) {
            return left.intent.confidence > right.intent.confidence;
        }
        return left.score > right.score;
    });

    if (result.ranked.empty()) {
        return result;
    }

    if (result.ranked.size() >= 2U) {
        const float margin = result.ranked[0].score - result.ranked[1].score;
        if (result.ranked[1].score > 0.50F && margin < ambiguityThreshold) {
            result.ambiguity = AmbiguityError{
                requestedTarget,
                {result.ranked[0], result.ranked[1]},
                "Ambiguous target: multiple intents scored similarly"};
            return result;
        }
    }

    result.bestMatch = result.ranked[0];
    return result;
}

float IntentResolver::LabelScore(const Intent& intent, const std::wstring& requestedTarget) const {
    if (requestedTarget.empty()) {
        return 0.75F;
    }

    const std::wstring requested = Lower(requestedTarget);
    const std::wstring candidate = Lower(PrimaryLabel(intent));

    if (candidate == requested) {
        return 1.0F;
    }

    if (candidate.find(requested) != std::wstring::npos) {
        return 0.72F;
    }

    return 0.0F;
}

float IntentResolver::DepthScore(const Intent& intent) const {
    const int depth = std::max(0, intent.target.hierarchyDepth);
    return 1.0F / static_cast<float>(1 + depth);
}

float IntentResolver::ProximityScore(const Intent& intent, const POINT& cursor) const {
    if (intent.target.screenCenter.x == 0 && intent.target.screenCenter.y == 0) {
        return 0.45F;
    }

    const double dx = static_cast<double>(intent.target.screenCenter.x - cursor.x);
    const double dy = static_cast<double>(intent.target.screenCenter.y - cursor.y);
    const double distance = std::sqrt(dx * dx + dy * dy);
    return static_cast<float>(1.0 / (1.0 + (distance / 400.0)));
}

float IntentResolver::FocusScore(const Intent& intent) const {
    return intent.target.focused ? 1.0F : 0.0F;
}

float IntentResolver::RecencyScore(
    const Intent& intent,
    const std::unordered_map<std::string, std::uint64_t>& recencyByIntentId) const {
    const auto it = recencyByIntentId.find(intent.id);
    if (it == recencyByIntentId.end()) {
        return 0.20F;
    }

    const std::uint64_t nowTicks = static_cast<std::uint64_t>(GetTickCount64());
    const std::uint64_t ageMs = nowTicks > it->second ? nowTicks - it->second : 0;
    return static_cast<float>(1.0 / (1.0 + (static_cast<double>(ageMs) / 5000.0)));
}

}  // namespace iee
