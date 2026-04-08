#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Adapter.h"
#include "EventBus.h"
#include "Intent.h"
#include "Telemetry.h"

namespace iee {

class ExecutionEngine {
public:
    ExecutionEngine(
        AdapterRegistry& adapters,
        EventBus& eventBus,
        const IntentValidator& validator,
        Telemetry& telemetry);

    ExecutionResult Execute(const Intent& intent);
    ExecutionResult ExecuteWithBudget(const Intent& intent, std::chrono::milliseconds latencyBudget);
    std::vector<AdapterMetadata> ListAdapterMetadata() const;

    EventBus& Events() {
        return eventBus_;
    }

    const EventBus& Events() const {
        return eventBus_;
    }

private:
    struct FastPathKey {
        IntentAction action{IntentAction::Unknown};
        TargetType targetType{TargetType::Unknown};
        std::wstring target;
        std::size_t paramsHash{0};

        bool operator==(const FastPathKey& other) const {
            return action == other.action && targetType == other.targetType && target == other.target &&
                paramsHash == other.paramsHash;
        }
    };

    struct FastPathKeyHash {
        std::size_t operator()(const FastPathKey& key) const {
            const std::size_t actionHash = std::hash<int>{}(static_cast<int>(key.action));
            const std::size_t targetTypeHash = std::hash<int>{}(static_cast<int>(key.targetType));
            const std::size_t textHash = std::hash<std::wstring>{}(key.target);
            const std::size_t paramsHash = std::hash<std::size_t>{}(key.paramsHash);
            return actionHash ^ (targetTypeHash << 1U) ^ (textHash << 2U) ^ (paramsHash << 3U);
        }
    };

    struct FastPathEntry {
        std::shared_ptr<Adapter> adapter;
        std::uint64_t snapshotTicks{0};
        std::uint64_t snapshotVersion{0};
        std::chrono::steady_clock::time_point cachedAt{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point accessedAt{std::chrono::steady_clock::now()};
    };

    ExecutionResult ExecuteInternal(const Intent& intent, int timeoutOverrideMs);
    ExecutionResult ExecuteWithRecovery(const Intent& intent, const std::shared_ptr<Adapter>& adapter, int timeoutOverrideMs);
    bool ShouldRetry(const Intent& intent, const ExecutionResult& lastResult, int attempt) const;
    FastPathKey BuildFastPathKey(const Intent& intent) const;
    std::shared_ptr<Adapter> TryFastPath(const Intent& intent, AdapterDecision* decision);
    void UpdateFastPath(const Intent& intent, const std::shared_ptr<Adapter>& adapter, bool successful);
    static std::wstring PrimaryTargetText(const Intent& intent);
    static std::size_t HashParams(const Params& params);

    AdapterRegistry& adapters_;
    EventBus& eventBus_;
    const IntentValidator& validator_;
    Telemetry& telemetry_;

    mutable std::mutex fastPathMutex_;
    std::unordered_map<FastPathKey, FastPathEntry, FastPathKeyHash> fastPath_;
};

}  // namespace iee
