#pragma once

#include <Windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Intent.h"

namespace iee {

struct IntentMatch {
    Intent intent;
    float score{0.0F};
    float depthScore{0.0F};
    float proximityScore{0.0F};
    float focusScore{0.0F};
    float recencyScore{0.0F};
};

struct AmbiguityError {
    std::wstring requestedTarget;
    std::vector<IntentMatch> candidates;
    std::string message;
};

struct ResolutionResult {
    std::optional<IntentMatch> bestMatch;
    std::optional<AmbiguityError> ambiguity;
    std::vector<IntentMatch> ranked;
};

class IntentResolver {
public:
    ResolutionResult Resolve(
        IntentAction action,
        const std::wstring& requestedTarget,
        const std::vector<Intent>& intents,
        const POINT& cursor,
        const std::unordered_map<std::string, std::uint64_t>& recencyByIntentId,
        float ambiguityThreshold = 0.08F) const;

private:
    float LabelScore(const Intent& intent, const std::wstring& requestedTarget) const;
    float DepthScore(const Intent& intent) const;
    float ProximityScore(const Intent& intent, const POINT& cursor) const;
    float FocusScore(const Intent& intent) const;
    float RecencyScore(const Intent& intent, const std::unordered_map<std::string, std::uint64_t>& recencyByIntentId) const;
};

}  // namespace iee
