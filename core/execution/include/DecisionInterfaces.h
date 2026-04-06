#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "EnvironmentAdapter.h"
#include "Intent.h"

namespace iee {

using StateSnapshot = EnvironmentState;

class DecisionProvider {
public:
    virtual ~DecisionProvider() = default;

    virtual std::string Name() const = 0;
    virtual std::vector<Intent> Decide(
        const StateSnapshot& state,
        std::chrono::milliseconds budget,
        std::string* diagnostics = nullptr) = 0;

    std::vector<Intent> decide(const StateSnapshot& state) {
        return Decide(state, std::chrono::milliseconds(0), nullptr);
    }
};

class Predictor {
public:
    virtual ~Predictor() = default;

    virtual std::string Name() const = 0;
    virtual StateSnapshot Predict(
        const Intent& intent,
        const StateSnapshot& current,
        std::string* diagnostics = nullptr) = 0;

    StateSnapshot predict(const Intent& intent, const StateSnapshot& current) {
        return Predict(intent, current, nullptr);
    }
};

struct FeedbackDelta {
    bool signatureChanged{false};
    double focusRatioDelta{0.0};
    double occupancyRatioDelta{0.0};
    std::int64_t captureSkewMs{0};
};

struct Feedback {
    Intent intent;
    ExecutionStatus status{ExecutionStatus::FAILED};
    StateSnapshot before;
    StateSnapshot after;
    FeedbackDelta delta;
    bool mismatch{false};
    std::string traceId;
    std::uint64_t frame{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

FeedbackDelta ComputeFeedbackDelta(const StateSnapshot& before, const StateSnapshot& after);
bool IsTargetVisible(const StateSnapshot& state, const std::wstring& target);

}  // namespace iee
