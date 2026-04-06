#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

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

private:
    struct FastPathKey {
        IntentAction action{IntentAction::Unknown};
        TargetType targetType{TargetType::Unknown};
        std::wstring target;

        bool operator==(const FastPathKey& other) const {
            return action == other.action && targetType == other.targetType && target == other.target;
        }
    };

    struct FastPathKeyHash {
        std::size_t operator()(const FastPathKey& key) const {
            const std::size_t actionHash = std::hash<int>{}(static_cast<int>(key.action));
            const std::size_t targetTypeHash = std::hash<int>{}(static_cast<int>(key.targetType));
            const std::size_t textHash = std::hash<std::wstring>{}(key.target);
            return actionHash ^ (targetTypeHash << 1U) ^ (textHash << 2U);
        }
    };

    struct FastPathEntry {
        std::weak_ptr<Adapter> adapter;
        std::uint64_t snapshotTicks{0};
        std::chrono::steady_clock::time_point cachedAt{std::chrono::steady_clock::now()};
    };

    ExecutionResult ExecuteWithRecovery(const Intent& intent, const std::shared_ptr<Adapter>& adapter);
    bool ShouldRetry(const Intent& intent, const ExecutionResult& lastResult, int attempt) const;
    FastPathKey BuildFastPathKey(const Intent& intent) const;
    std::shared_ptr<Adapter> TryFastPath(const Intent& intent, AdapterDecision* decision);
    void UpdateFastPath(const Intent& intent, const std::shared_ptr<Adapter>& adapter, bool successful);
    static std::wstring PrimaryTargetText(const Intent& intent);

    AdapterRegistry& adapters_;
    EventBus& eventBus_;
    const IntentValidator& validator_;
    Telemetry& telemetry_;

    mutable std::mutex fastPathMutex_;
    std::unordered_map<FastPathKey, FastPathEntry, FastPathKeyHash> fastPath_;
};

}  // namespace iee
