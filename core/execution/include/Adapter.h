#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AccessibilityLayer.h"
#include "CapabilityGraph.h"
#include "EventBus.h"
#include "Intent.h"
#include "ObserverEngine.h"

namespace iee {

struct ExecutionResult {
    ExecutionStatus status{ExecutionStatus::FAILED};
    bool verified{false};
    bool usedFallback{false};
    std::string method;
    std::string message;
    std::chrono::milliseconds duration{0};
    int attempts{0};
    std::string traceId;

    bool IsSuccess() const {
        return status == ExecutionStatus::SUCCESS || status == ExecutionStatus::PARTIAL;
    }
};

struct TimedIntent {
    Intent intent;
    int delay_ms{0};
    int hold_ms{0};
    int sequence_ms{0};
};

struct AdapterScore {
    float reliability{0.50F};
    float latency{80.0F};
    float confidence{0.50F};
};

struct AdapterMetadata {
    std::string name;
    std::string version{"1.0"};
    int priority{100};
    std::vector<std::string> supportedActions;
};

struct AdapterDecisionCandidate {
    std::string adapterName;
    AdapterScore score;
    float finalScore{0.0F};
    bool matched{false};
};

struct AdapterDecision {
    std::string selectedAdapter;
    std::vector<AdapterDecisionCandidate> candidates;
    bool usedFastPath{false};
};

class Adapter {
public:
    virtual ~Adapter() = default;

    virtual std::string Name() const = 0;
    virtual std::vector<Intent> GetCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph& graph) = 0;
    virtual bool CanExecute(const Intent& intent) const = 0;
    virtual ExecutionResult Execute(const Intent& intent) = 0;

    // SDK-form aliases
    std::string name() const {
        return Name();
    }
    std::vector<Intent> getCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph& graph) {
        return GetCapabilities(snapshot, graph);
    }
    ExecutionResult execute(const Intent& intent) {
        return Execute(intent);
    }

    // Adapter baseline score used by runtime reliability selection.
    virtual AdapterScore GetScore() const;

    // Adapter metadata for ecosystem discovery and deterministic registration.
    virtual AdapterMetadata GetMetadata() const;

    AdapterScore getScore() const {
        return GetScore();
    }

    AdapterMetadata getMetadata() const {
        return GetMetadata();
    }

    // Optional event subscription hook for adapter-specific reactive behavior.
    virtual void Subscribe(EventBus& bus);

    void subscribe(EventBus& bus) {
        Subscribe(bus);
    }
};

class UIAAdapter : public Adapter {
public:
    explicit UIAAdapter(IAccessibilityLayer& accessibilityLayer);

    std::string Name() const override;
    std::vector<Intent> GetCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph& graph) override;
    bool CanExecute(const Intent& intent) const override;
    ExecutionResult Execute(const Intent& intent) override;
    AdapterScore GetScore() const override;

private:
    Intent BuildIntentFromElement(const UiElement& element, const ObserverSnapshot& snapshot, IntentAction action) const;

    IAccessibilityLayer& accessibilityLayer_;
};

class VSCodeAdapter : public Adapter {
public:
    explicit VSCodeAdapter(IAccessibilityLayer& accessibilityLayer);

    std::string Name() const override;
    std::vector<Intent> GetCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph& graph) override;
    bool CanExecute(const Intent& intent) const override;
    ExecutionResult Execute(const Intent& intent) override;
    AdapterScore GetScore() const override;

private:
    static bool IsVsCodeSnapshot(const ObserverSnapshot& snapshot);
    static bool IsVsCodeIntent(const Intent& intent);
    static bool IsVsCodeTargetHint(const std::wstring& value);

    UIAAdapter delegate_;
};

class InputAdapter : public Adapter {
public:
    std::string Name() const override;
    std::vector<Intent> GetCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph& graph) override;
    bool CanExecute(const Intent& intent) const override;
    ExecutionResult Execute(const Intent& intent) override;
    AdapterScore GetScore() const override;

private:
    static int ReadTimingParamMs(const Intent& intent, std::string_view key, int defaultValue, int maxValue);
    static bool SendUnicodeText(const std::wstring& text, int holdMs);
    static bool SendLeftClick(int x, int y, int holdMs);
    static bool SendVirtualKey(WORD keyCode, int holdMs);
};

class FileSystemAdapter : public Adapter {
public:
    std::string Name() const override;
    std::vector<Intent> GetCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph& graph) override;
    bool CanExecute(const Intent& intent) const override;
    ExecutionResult Execute(const Intent& intent) override;
    AdapterScore GetScore() const override;

private:
    Intent BuildIntentFromPath(const ObserverSnapshot& snapshot, IntentAction action, const std::wstring& path) const;
};

class AdapterRegistry {
public:
    void Register(std::unique_ptr<Adapter> adapter);
    void RegisterAdapter(std::shared_ptr<Adapter> adapter);

    void registerAdapter(std::shared_ptr<Adapter> adapter) {
        RegisterAdapter(std::move(adapter));
    }

    std::vector<Adapter*> GetAll() const;
    std::vector<std::shared_ptr<Adapter>> GetAdapters() const;

    std::vector<AdapterMetadata> ListMetadata() const;

    std::vector<std::shared_ptr<Adapter>> getAdapters() const {
        return GetAdapters();
    }

    // Legacy pointer-based resolver preserved for compatibility.
    Adapter* Resolve(const Intent& intent) const;

    // Score-based deterministic selection entrypoint.
    std::shared_ptr<Adapter> ResolveBest(const Intent& intent) const;
    std::shared_ptr<Adapter> ResolveBest(const Intent& intent, AdapterDecision* decision) const;

    std::shared_ptr<Adapter> resolveBest(const Intent& intent) const {
        return ResolveBest(intent);
    }

    // Records runtime outcome for rolling reliability and latency tracking.
    void RecordExecution(const Adapter& adapter, const ExecutionResult& result);

private:
    struct AdapterRuntime {
        float reliabilityEma{0.50F};
        float latencyEmaMs{80.0F};
        float confidence{0.50F};
        std::uint64_t successCount{0};
        std::uint64_t failureCount{0};
        std::size_t registrationOrder{0};
        std::chrono::steady_clock::time_point lastUpdated{};
    };

    static float Clamp01(float value);
    static float ClampMin(float value, float minimum);
    AdapterScore ComputeDecayedScore(const Adapter& adapter, const AdapterRuntime& runtime) const;

    mutable std::shared_mutex mutex_;
    std::vector<std::shared_ptr<Adapter>> adapters_;
    std::unordered_map<const Adapter*, AdapterRuntime> runtimeByAdapter_;
};

}  // namespace iee
