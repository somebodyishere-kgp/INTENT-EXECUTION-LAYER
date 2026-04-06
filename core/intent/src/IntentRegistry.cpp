#include "IntentRegistry.h"

#include <algorithm>
#include <cwctype>
#include <exception>
#include <functional>
#include <sstream>

#include "Logger.h"

namespace iee {
namespace {

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(::towlower(c));
    });
    return value;
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
}

std::uint64_t NowTicks() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

}  // namespace

IntentRegistry::IntentRegistry(
    IObserverEngine& observer,
    AdapterRegistry& adapters,
    CapabilityGraphBuilder& graphBuilder,
    IntentResolver& resolver,
        EventBus& eventBus,
        Telemetry& telemetry)
    : observer_(observer),
      adapters_(adapters),
      graphBuilder_(graphBuilder),
      resolver_(resolver),
            eventBus_(eventBus),
            telemetry_(telemetry) {
    uiChangedSubscriptionId_ = eventBus_.Subscribe(EventType::UiChanged, [this](const Event&) {
        RefreshUiIncremental();
    });

    fileChangedSubscriptionId_ = eventBus_.Subscribe(EventType::FileSystemChanged, [this](const Event&) {
        RefreshFileSystemIncremental();
    });
}

void IntentRegistry::Refresh() {
    const ObserverSnapshot snapshot = observer_.Capture();
    if (!snapshot.valid) {
        Logger::Warning("IntentRegistry", "Snapshot invalid; skipping refresh");
        return;
    }

    CapabilityGraph newGraph;
    std::vector<Intent> newIntents;

    try {
        newGraph = graphBuilder_.Build(snapshot);

        for (Adapter* adapter : adapters_.GetAll()) {
            if (adapter == nullptr) {
                continue;
            }

            auto adapterIntents = adapter->GetCapabilities(snapshot, newGraph);
            for (auto& intent : adapterIntents) {
                if (intent.id.empty()) {
                    intent.id = BuildIntentId(intent);
                }
                if (intent.target.type == TargetType::Unknown) {
                    intent.target.type =
                        (intent.action == IntentAction::Create || intent.action == IntentAction::Delete || intent.action == IntentAction::Move)
                        ? TargetType::FileSystemPath
                        : TargetType::UiElement;
                }

                newIntents.push_back(std::move(intent));
            }
        }
    } catch (const std::exception& ex) {
        const std::string message = std::string("Registry refresh failed: ") + ex.what();
        Logger::Error("IntentRegistry", message);
        eventBus_.Publish(Event{EventType::Error, "IntentRegistry", message, std::chrono::system_clock::now()});
        return;
    }

    std::sort(
        newIntents.begin(),
        newIntents.end(),
        [](const Intent& left, const Intent& right) {
            if (left.action != right.action) {
                return ToString(left.action) < ToString(right.action);
            }
            if (left.confidence != right.confidence) {
                return left.confidence > right.confidence;
            }
            return IntentRegistry::PrimaryTargetText(left) < IntentRegistry::PrimaryTargetText(right);
        });

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastSnapshot_ = snapshot;
        graph_ = std::move(newGraph);
        intents_ = std::move(newIntents);
        resolutionCache_.clear();
    }

    eventBus_.Publish(Event{
        EventType::RegistryUpdated,
        "IntentRegistry",
        "Intent registry refreshed",
        std::chrono::system_clock::now(),
        EventPriority::MEDIUM});

    Logger::Info("IntentRegistry", "Refreshed intent registry");
}

void IntentRegistry::RefreshUiIncremental() {
    const ObserverSnapshot snapshot = observer_.Capture();
    if (!snapshot.valid) {
        return;
    }

    std::vector<Intent> uiIntents;
    CapabilityGraph graphCopy;

    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        graphCopy = graph_;
    }

    try {
        for (Adapter* adapter : adapters_.GetAll()) {
            if (adapter == nullptr || adapter->Name() != "UIAAdapter") {
                continue;
            }

            auto adapterIntents = adapter->GetCapabilities(snapshot, graphCopy);
            for (auto& intent : adapterIntents) {
                if (intent.id.empty()) {
                    intent.id = BuildIntentId(intent);
                }
                uiIntents.push_back(std::move(intent));
            }
        }
    } catch (const std::exception& ex) {
        Logger::Error("IntentRegistry", "UI refresh failed: " + std::string(ex.what()));
        return;
    } catch (...) {
        Logger::Error("IntentRegistry", "UI refresh failed: unknown exception");
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastSnapshot_ = snapshot;
        ReplaceIntentsBySource("uia", std::move(uiIntents));
        resolutionCache_.clear();
    }

    eventBus_.Publish(Event{EventType::RegistryUpdated, "IntentRegistry", "Incremental UI intent refresh applied", std::chrono::system_clock::now(), EventPriority::HIGH});
}

void IntentRegistry::RefreshFileSystemIncremental() {
    const ObserverSnapshot snapshot = observer_.Capture();
    if (!snapshot.valid) {
        return;
    }

    std::vector<Intent> fsIntents;
    CapabilityGraph graphCopy;

    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        graphCopy = graph_;
    }

    try {
        for (Adapter* adapter : adapters_.GetAll()) {
            if (adapter == nullptr || adapter->Name() != "FileSystemAdapter") {
                continue;
            }

            auto adapterIntents = adapter->GetCapabilities(snapshot, graphCopy);
            for (auto& intent : adapterIntents) {
                if (intent.id.empty()) {
                    intent.id = BuildIntentId(intent);
                }
                fsIntents.push_back(std::move(intent));
            }
        }
    } catch (const std::exception& ex) {
        Logger::Error("IntentRegistry", "Filesystem refresh failed: " + std::string(ex.what()));
        return;
    } catch (...) {
        Logger::Error("IntentRegistry", "Filesystem refresh failed: unknown exception");
        return;
    }

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        lastSnapshot_ = snapshot;
        ReplaceIntentsBySource("filesystem", std::move(fsIntents));
        resolutionCache_.clear();
    }

    eventBus_.Publish(Event{EventType::RegistryUpdated, "IntentRegistry", "Incremental filesystem intent refresh applied", std::chrono::system_clock::now(), EventPriority::LOW});
}

std::vector<Intent> IntentRegistry::ListIntents() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return intents_;
}

std::optional<Intent> IntentRegistry::FindById(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& intent : intents_) {
        if (intent.id == id) {
            return intent;
        }
    }

    return std::nullopt;
}

ResolutionResult IntentRegistry::Resolve(IntentAction action, const std::wstring& target) const {
    const auto resolveStart = std::chrono::steady_clock::now();

    std::vector<Intent> intentsCopy;
    POINT cursor{};
    std::unordered_map<std::string, std::uint64_t> recencyCopy;
    ResolutionCacheKey key;

    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        key.action = action;
        key.target = NormalizeTarget(target);
        key.snapshotSequence = lastSnapshot_.sequence;

        const auto cacheIt = resolutionCache_.find(key);
        if (cacheIt != resolutionCache_.end()) {
            telemetry_.LogResolutionTiming(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - resolveStart));
            return cacheIt->second;
        }

        intentsCopy = intents_;
        cursor = lastSnapshot_.cursorPosition;
        recencyCopy = recencyByIntentId_;
    }

    ResolutionResult result = resolver_.Resolve(
        action,
        target,
        intentsCopy,
        cursor,
        recencyCopy);

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (key.snapshotSequence == lastSnapshot_.sequence) {
            resolutionCache_[key] = result;
            if (resolutionCache_.size() > 512U) {
                std::size_t removed = 0;
                for (auto it = resolutionCache_.begin(); it != resolutionCache_.end() && removed < 128U;) {
                    it = resolutionCache_.erase(it);
                    ++removed;
                }
            }
        }
    }

    if (result.ambiguity.has_value()) {
        eventBus_.Publish(Event{
            EventType::AmbiguityDetected,
            "IntentRegistry",
            result.ambiguity->message,
            std::chrono::system_clock::now(),
            EventPriority::HIGH});
    }

    telemetry_.LogResolutionTiming(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - resolveStart));
    return result;
}

CapabilityGraph IntentRegistry::Graph() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return graph_;
}

ObserverSnapshot IntentRegistry::LastSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return lastSnapshot_;
}

void IntentRegistry::RecordInteraction(const std::string& intentId) {
    if (intentId.empty()) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    recencyByIntentId_[intentId] = NowTicks();
    resolutionCache_.clear();
}

std::string IntentRegistry::BuildIntentId(const Intent& intent) const {
    std::hash<std::string> narrowHasher;
    std::hash<std::wstring> wideHasher;

    const std::wstring text = PrimaryTargetText(intent);

    const std::size_t seed = narrowHasher(ToString(intent.action)) ^
        (wideHasher(text) << 1U) ^
        (narrowHasher(intent.source) << 2U);

    std::ostringstream stream;
    stream << "intent-" << std::hex << seed;
    return stream.str();
}

void IntentRegistry::ReplaceIntentsBySource(const std::string& source, std::vector<Intent> replacement) {
    std::vector<Intent> retained;
    retained.reserve(intents_.size() + replacement.size());

    for (const auto& intent : intents_) {
        if (LowerAscii(intent.source) == LowerAscii(source)) {
            continue;
        }
        retained.push_back(intent);
    }

    for (auto& intent : replacement) {
        retained.push_back(std::move(intent));
    }

    std::sort(
        retained.begin(),
        retained.end(),
        [](const Intent& left, const Intent& right) {
            if (left.action != right.action) {
                return ToString(left.action) < ToString(right.action);
            }
            if (left.confidence != right.confidence) {
                return left.confidence > right.confidence;
            }
            return IntentRegistry::PrimaryTargetText(left) < IntentRegistry::PrimaryTargetText(right);
        });

    intents_ = std::move(retained);
}

std::wstring IntentRegistry::PrimaryTargetText(const Intent& intent) {
    if (!intent.target.label.empty()) {
        return intent.target.label;
    }
    if (!intent.target.automationId.empty()) {
        return intent.target.automationId;
    }
    return intent.target.path;
}

std::wstring IntentRegistry::NormalizeTarget(const std::wstring& target) {
    return Lower(target);
}

}  // namespace iee
