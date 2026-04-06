#include "DecisionInterfaces.h"

#include <algorithm>
#include <cmath>

namespace iee {
namespace {

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        if (ch >= L'A' && ch <= L'Z') {
            return static_cast<wchar_t>(ch - L'A' + L'a');
        }
        return ch;
    });
    return value;
}

}  // namespace

FeedbackDelta ComputeFeedbackDelta(const StateSnapshot& before, const StateSnapshot& after) {
    FeedbackDelta delta;
    delta.signatureChanged = before.perception.uiSignature != after.perception.uiSignature;
    delta.focusRatioDelta = after.perception.focusRatio - before.perception.focusRatio;
    delta.occupancyRatioDelta = after.perception.occupancyRatio - before.perception.occupancyRatio;
    delta.captureSkewMs = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(after.capturedAt - before.capturedAt).count());
    return delta;
}

bool IsTargetVisible(const StateSnapshot& state, const std::wstring& target) {
    if (target.empty()) {
        return false;
    }

    const std::wstring normalizedTarget = ToLower(target);
    for (const UiElement& element : state.uiElements) {
        const std::wstring name = ToLower(element.name);
        const std::wstring automationId = ToLower(element.automationId);
        if (name == normalizedTarget || automationId == normalizedTarget) {
            return true;
        }
    }

    return false;
}

}  // namespace iee
