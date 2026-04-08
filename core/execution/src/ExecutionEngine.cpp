#include "ExecutionEngine.h"

#include <algorithm>
#include <cwctype>
#include <limits>

#include "Logger.h"

namespace iee {
namespace {

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(::towlower(ch));
    });
    return value;
}

void HashCombineInPlace(std::size_t* seed, std::size_t value) {
    *seed ^= value + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) + (*seed << 6U) + (*seed >> 2U);
}

}  // namespace

ExecutionEngine::ExecutionEngine(
    AdapterRegistry& adapters,
    EventBus& eventBus,
    const IntentValidator& validator,
    Telemetry& telemetry)
    : adapters_(adapters),
      eventBus_(eventBus),
      validator_(validator),
      telemetry_(telemetry) {}

ExecutionResult ExecutionEngine::Execute(const Intent& intent) {
    return ExecuteInternal(intent, -1);
}

ExecutionResult ExecutionEngine::ExecuteWithBudget(const Intent& intent, std::chrono::milliseconds latencyBudget) {
    int timeoutOverrideMs = -1;
    if (latencyBudget.count() > 0) {
        const auto bounded = std::min<long long>(
            latencyBudget.count(),
            static_cast<long long>(std::numeric_limits<int>::max()));
        timeoutOverrideMs = static_cast<int>(bounded);
    }

    return ExecuteInternal(intent, timeoutOverrideMs);
}

std::vector<AdapterMetadata> ExecutionEngine::ListAdapterMetadata() const {
    return adapters_.ListMetadata();
}

ExecutionResult ExecutionEngine::ExecuteInternal(const Intent& intent, int timeoutOverrideMs) {
    const std::string traceId = telemetry_.NewTraceId();

    std::string validationError;
    if (!validator_.Validate(intent, &validationError)) {
        Logger::Error("ExecutionEngine", "Intent validation failed: " + validationError);
        telemetry_.LogFailure(traceId, "validator", validationError);
        eventBus_.Publish(Event{
            EventType::Error,
            "ExecutionEngine",
            "Intent validation failed: " + validationError,
            std::chrono::system_clock::now(),
            EventPriority::HIGH});

        ExecutionResult invalidResult;
        invalidResult.status = ExecutionStatus::FAILED;
        invalidResult.verified = false;
        invalidResult.method = "validator";
        invalidResult.message = validationError;
        invalidResult.traceId = traceId;
        return invalidResult;
    }

    eventBus_.Publish(Event{
        EventType::ExecutionStarted,
        "ExecutionEngine",
        "Execution started",
        std::chrono::system_clock::now(),
        EventPriority::HIGH});

    AdapterDecision decision;
    std::shared_ptr<Adapter> adapter = TryFastPath(intent, &decision);
    if (adapter == nullptr) {
        adapter = adapters_.ResolveBest(intent, &decision);
    }

    std::vector<std::pair<std::string, float>> rankedScores;
    rankedScores.reserve(decision.candidates.size());
    for (const auto& candidate : decision.candidates) {
        if (candidate.matched) {
            rankedScores.push_back({candidate.adapterName, candidate.finalScore});
        }
    }

    telemetry_.LogAdapterDecision(
        traceId,
        intent.action,
        PrimaryTargetText(intent),
        decision.selectedAdapter,
        rankedScores,
        decision.usedFastPath);

    if (adapter == nullptr) {
        eventBus_.Publish(Event{
            EventType::Error,
            "ExecutionEngine",
            "No adapter available for intent",
            std::chrono::system_clock::now(),
            EventPriority::HIGH});

        ExecutionResult noAdapterResult;
        noAdapterResult.status = ExecutionStatus::FAILED;
        noAdapterResult.verified = false;
        noAdapterResult.method = "none";
        noAdapterResult.message = "No adapter available for action: " + ToString(intent.action);
        noAdapterResult.traceId = traceId;

        telemetry_.LogFailure(traceId, "adapter-resolution", noAdapterResult.message);
        telemetry_.LogExecution(ExecutionTrace{
            traceId,
            ToString(intent.action),
            PrimaryTargetText(intent),
            "none",
            0,
            ToString(noAdapterResult.status),
            noAdapterResult.message,
            noAdapterResult.verified,
            noAdapterResult.attempts,
            intent.context.snapshotVersion,
            intent.context.controlFrame,
            std::chrono::system_clock::now()});

        return noAdapterResult;
    }

    std::shared_ptr<Adapter> usedAdapter = adapter;
    bool fallbackUsed = false;
    ExecutionResult result = ExecuteWithRecovery(intent, adapter, timeoutOverrideMs);
    if (result.status == ExecutionStatus::FAILED && intent.constraints.allowFallback) {
        for (const auto& candidate : adapters_.GetAdapters()) {
            if (candidate == nullptr || candidate == adapter || !candidate->CanExecute(intent)) {
                continue;
            }

            Logger::Warning("ExecutionEngine", "Trying fallback adapter: " + candidate->Name());
            eventBus_.Publish(Event{
                EventType::ExecutionFallback,
                "ExecutionEngine",
                "Fallback adapter selected: " + candidate->Name(),
                std::chrono::system_clock::now(),
                EventPriority::HIGH});

            ExecutionResult fallbackResult = ExecuteWithRecovery(intent, candidate, timeoutOverrideMs);
            if (fallbackResult.status == ExecutionStatus::SUCCESS || fallbackResult.status == ExecutionStatus::PARTIAL) {
                result = fallbackResult;
                usedAdapter = candidate;
                fallbackUsed = true;
                break;
            }
        }
    }

    result.traceId = traceId;
    result.usedFallback = fallbackUsed;
    if (result.method.empty() && usedAdapter != nullptr) {
        result.method = usedAdapter->Name();
    }

    UpdateFastPath(intent, usedAdapter, result.IsSuccess());

    telemetry_.LogExecution(ExecutionTrace{
        traceId,
        ToString(intent.action),
        PrimaryTargetText(intent),
        result.method,
        result.duration.count(),
        ToString(result.status),
        result.message,
        result.verified,
        result.attempts,
        intent.context.snapshotVersion,
        intent.context.controlFrame,
        std::chrono::system_clock::now()});

    if (result.status == ExecutionStatus::FAILED) {
        telemetry_.LogFailure(traceId, "ExecutionEngine", result.message);
    }

    const std::string completion =
        (result.status == ExecutionStatus::SUCCESS || result.status == ExecutionStatus::PARTIAL)
        ? "Execution completed"
        : "Execution failed";

    eventBus_.Publish(Event{
        EventType::ExecutionFinished,
        "ExecutionEngine",
        completion + ": " + result.message,
        std::chrono::system_clock::now(),
        result.status == ExecutionStatus::FAILED ? EventPriority::HIGH : EventPriority::MEDIUM});

    return result;
}

ExecutionResult ExecutionEngine::ExecuteWithRecovery(
    const Intent& intent,
    const std::shared_ptr<Adapter>& adapter,
    int timeoutOverrideMs) {
    ExecutionResult lastResult;
    if (adapter == nullptr) {
        lastResult.status = ExecutionStatus::FAILED;
        lastResult.message = "Null adapter";
        lastResult.method = "none";
        return lastResult;
    }

    int effectiveTimeoutMs = intent.constraints.timeoutMs;
    if (timeoutOverrideMs > 0) {
        if (effectiveTimeoutMs > 0) {
            effectiveTimeoutMs = std::min(effectiveTimeoutMs, timeoutOverrideMs);
        } else {
            effectiveTimeoutMs = timeoutOverrideMs;
        }
    }
    if (effectiveTimeoutMs <= 0) {
        effectiveTimeoutMs = 1;
    }

    const int maxRetries = std::max(0, intent.constraints.maxRetries);
    const auto start = std::chrono::steady_clock::now();
    const auto cumulativeTimeoutMs = static_cast<long long>(effectiveTimeoutMs) * static_cast<long long>(maxRetries + 1);
    const auto deadline = start + std::chrono::milliseconds(cumulativeTimeoutMs);

    for (int attempt = 0; attempt <= maxRetries; ++attempt) {
        if (std::chrono::steady_clock::now() >= deadline) {
            lastResult.status = ExecutionStatus::FAILED;
            lastResult.verified = false;
            lastResult.method = adapter->Name();
            lastResult.message = "Cumulative timeout exceeded";
            lastResult.attempts = attempt;
            break;
        }

        const auto begin = std::chrono::steady_clock::now();
        lastResult = adapter->Execute(intent);
        const auto end = std::chrono::steady_clock::now();

        if (lastResult.duration.count() <= 0) {
            lastResult.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
        }

        if (lastResult.method.empty()) {
            lastResult.method = adapter->Name();
        }

        if (lastResult.duration.count() > effectiveTimeoutMs) {
            lastResult.status = ExecutionStatus::FAILED;
            lastResult.verified = false;
            if (!lastResult.message.empty()) {
                lastResult.message += "; ";
            }
            lastResult.message += "Timeout exceeded";
        }

        if (std::chrono::steady_clock::now() >= deadline && lastResult.status != ExecutionStatus::SUCCESS) {
            lastResult.status = ExecutionStatus::FAILED;
            lastResult.verified = false;
            if (!lastResult.message.empty()) {
                lastResult.message += "; ";
            }
            lastResult.message += "Cumulative timeout exceeded";
        }

        lastResult.attempts = attempt + 1;
        adapters_.RecordExecution(*adapter, lastResult);

        if (lastResult.status == ExecutionStatus::SUCCESS) {
            return lastResult;
        }

        if (!ShouldRetry(intent, lastResult, attempt)) {
            break;
        }

        Logger::Warning("ExecutionEngine", "Retrying intent execution using adapter " + adapter->Name());
        eventBus_.Publish(Event{
            EventType::ExecutionRetry,
            "ExecutionEngine",
            "Retry attempt " + std::to_string(attempt + 1),
            std::chrono::system_clock::now(),
            EventPriority::HIGH});
    }

    return lastResult;
}

bool ExecutionEngine::ShouldRetry(const Intent& intent, const ExecutionResult& lastResult, int attempt) const {
    const int maxRetries = std::max(0, intent.constraints.maxRetries);
    const int normalizedAttempt = std::max(0, attempt);

    if (normalizedAttempt >= maxRetries) {
        return false;
    }

    if (lastResult.status == ExecutionStatus::SUCCESS) {
        return false;
    }

    if (lastResult.status == ExecutionStatus::PARTIAL && !intent.constraints.requiresVerification) {
        return false;
    }

    return true;
}

ExecutionEngine::FastPathKey ExecutionEngine::BuildFastPathKey(const Intent& intent) const {
    FastPathKey key;
    key.action = intent.action;
    key.targetType = intent.target.type;
    key.target = Lower(PrimaryTargetText(intent));
    key.paramsHash = HashParams(intent.params);
    return key;
}

std::shared_ptr<Adapter> ExecutionEngine::TryFastPath(const Intent& intent, AdapterDecision* decision) {
    if (intent.context.snapshotTicks == 0 && intent.context.snapshotVersion == 0) {
        return nullptr;
    }

    const FastPathKey key = BuildFastPathKey(intent);
    std::lock_guard<std::mutex> lock(fastPathMutex_);
    const auto it = fastPath_.find(key);
    if (it == fastPath_.end()) {
        return nullptr;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.cachedAt).count();
    if (age > 1000) {
        fastPath_.erase(it);
        return nullptr;
    }

    if (intent.context.snapshotTicks != 0 && it->second.snapshotTicks != intent.context.snapshotTicks) {
        fastPath_.erase(it);
        return nullptr;
    }

    if (intent.context.snapshotVersion != 0 && it->second.snapshotVersion != intent.context.snapshotVersion) {
        fastPath_.erase(it);
        return nullptr;
    }

    std::shared_ptr<Adapter> adapter = it->second.adapter;
    if (adapter == nullptr || !adapter->CanExecute(intent)) {
        fastPath_.erase(it);
        return nullptr;
    }

    it->second.accessedAt = now;

    if (decision != nullptr) {
        decision->selectedAdapter = adapter->Name();
        decision->usedFastPath = true;
    }
    return adapter;
}

void ExecutionEngine::UpdateFastPath(const Intent& intent, const std::shared_ptr<Adapter>& adapter, bool successful) {
    if (!successful || adapter == nullptr) {
        return;
    }

    if (intent.context.snapshotTicks == 0 && intent.context.snapshotVersion == 0) {
        return;
    }

    const FastPathKey key = BuildFastPathKey(intent);
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(fastPathMutex_);
    fastPath_[key] = FastPathEntry{
        adapter,
        intent.context.snapshotTicks,
        intent.context.snapshotVersion,
        now,
        now};

    if (fastPath_.size() <= 256U) {
        return;
    }

    for (std::size_t evicted = 0; evicted < 64U && !fastPath_.empty(); ++evicted) {
        auto oldest = fastPath_.begin();
        for (auto it = fastPath_.begin(); it != fastPath_.end(); ++it) {
            if (it->second.accessedAt < oldest->second.accessedAt) {
                oldest = it;
            }
        }
        fastPath_.erase(oldest);
    }
}

std::wstring ExecutionEngine::PrimaryTargetText(const Intent& intent) {
    if (!intent.target.label.empty()) {
        return intent.target.label;
    }
    if (!intent.target.automationId.empty()) {
        return intent.target.automationId;
    }
    return intent.target.path;
}

std::size_t ExecutionEngine::HashParams(const Params& params) {
    std::size_t seed = static_cast<std::size_t>(0x811c9dc5);

    for (const auto& entry : params.values) {
        HashCombineInPlace(&seed, std::hash<std::string>{}(entry.first));
        HashCombineInPlace(&seed, std::hash<std::wstring>{}(entry.second));
    }

    return seed;
}

}  // namespace iee