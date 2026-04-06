#pragma once

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "Adapter.h"
#include "CapabilityGraph.h"
#include "EventBus.h"
#include "Intent.h"
#include "IntentResolver.h"
#include "ObserverEngine.h"
#include "Telemetry.h"

namespace iee {

class IntentRegistry {
public:
    IntentRegistry(
        IObserverEngine& observer,
        AdapterRegistry& adapters,
        CapabilityGraphBuilder& graphBuilder,
        IntentResolver& resolver,
        EventBus& eventBus,
        Telemetry& telemetry);

    void Refresh();
    void RefreshUiIncremental();
    void RefreshFileSystemIncremental();

    std::vector<Intent> ListIntents() const;
    std::optional<Intent> FindById(const std::string& id) const;
    ResolutionResult Resolve(IntentAction action, const std::wstring& target) const;

    CapabilityGraph Graph() const;
    ObserverSnapshot LastSnapshot() const;
    void RecordInteraction(const std::string& intentId);

private:
    struct ResolutionCacheKey {
        IntentAction action{IntentAction::Unknown};
        std::wstring target;
        std::uint64_t snapshotSequence{0};
        std::uint64_t cacheEpoch{0};

        bool operator==(const ResolutionCacheKey& other) const {
            return action == other.action && target == other.target && snapshotSequence == other.snapshotSequence &&
                cacheEpoch == other.cacheEpoch;
        }
    };

    struct ResolutionCacheKeyHash {
        std::size_t operator()(const ResolutionCacheKey& key) const {
            const std::size_t actionHash = std::hash<int>{}(static_cast<int>(key.action));
            const std::size_t targetHash = std::hash<std::wstring>{}(key.target);
            const std::size_t sequenceHash = std::hash<std::uint64_t>{}(key.snapshotSequence);
            const std::size_t epochHash = std::hash<std::uint64_t>{}(key.cacheEpoch);
            return actionHash ^ (targetHash << 1U) ^ (sequenceHash << 2U) ^ (epochHash << 3U);
        }
    };

    std::string BuildIntentId(const Intent& intent) const;
    void ReplaceIntentsBySource(const std::string& source, std::vector<Intent> replacement);
    static std::wstring PrimaryTargetText(const Intent& intent);
    static std::wstring NormalizeTarget(const std::wstring& target);

    IObserverEngine& observer_;
    AdapterRegistry& adapters_;
    CapabilityGraphBuilder& graphBuilder_;
    IntentResolver& resolver_;
    EventBus& eventBus_;
    Telemetry& telemetry_;

    mutable std::shared_mutex mutex_;
    ObserverSnapshot lastSnapshot_;
    CapabilityGraph graph_;
    std::vector<Intent> intents_;
    std::unordered_map<std::string, std::uint64_t> recencyByIntentId_;
    std::uint64_t cacheEpoch_{0};
    mutable std::unordered_map<ResolutionCacheKey, ResolutionResult, ResolutionCacheKeyHash> resolutionCache_;

    EventBus::SubscriptionId uiChangedSubscriptionId_{0};
    EventBus::SubscriptionId fileChangedSubscriptionId_{0};
};

}  // namespace iee
