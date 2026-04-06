#include "Adapter.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

#include "Logger.h"

namespace iee {
namespace {

std::string Narrow(const std::wstring& value) {
    if (value.empty()) {
        return "";
    }

    const int requiredBytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (requiredBytes <= 1) {
        return "";
    }

    std::string result(static_cast<std::size_t>(requiredBytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), requiredBytes, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::uint64_t ToTicks(std::chrono::system_clock::time_point timestamp) {
    const auto sinceEpoch = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch());
    return static_cast<std::uint64_t>(sinceEpoch.count());
}

Target BuildTargetFromElement(const UiElement& element) {
    Target target;
    target.type = TargetType::UiElement;
    target.label = !element.name.empty() ? element.name : element.automationId;
    target.automationId = element.automationId;
    target.nodeId = element.id;
    target.hierarchyDepth = element.depth;
    target.focused = element.isFocused;
    target.screenCenter.x = element.bounds.left + ((element.bounds.right - element.bounds.left) / 2);
    target.screenCenter.y = element.bounds.top + ((element.bounds.bottom - element.bounds.top) / 2);
    return target;
}

Context BuildContext(const ObserverSnapshot& snapshot) {
    Context context;
    context.application = snapshot.activeProcessPath;
    context.windowTitle = snapshot.activeWindowTitle;
    context.cursor = snapshot.cursorPosition;
    context.activeWindow = snapshot.activeWindow;
    context.snapshotTicks = ToTicks(snapshot.capturedAt);

    std::error_code ec;
    context.workingDirectory = std::filesystem::current_path(ec).wstring();
    return context;
}

float CompositeScore(const AdapterScore& score) {
    const float latencyPenalty = std::clamp(score.latency / 1000.0F, 0.0F, 1.0F);
    constexpr float kReliabilityWeight = 0.55F;
    constexpr float kConfidenceWeight = 0.35F;
    constexpr float kLatencyWeight = 0.10F;

    return (kReliabilityWeight * score.reliability) +
        (kConfidenceWeight * score.confidence) -
        (kLatencyWeight * latencyPenalty);
}

}  // namespace

AdapterScore Adapter::GetScore() const {
    return AdapterScore{};
}

void Adapter::Subscribe(EventBus&) {
}

UIAAdapter::UIAAdapter(IAccessibilityLayer& accessibilityLayer)
    : accessibilityLayer_(accessibilityLayer) {}

std::string UIAAdapter::Name() const {
    return "UIAAdapter";
}

std::vector<Intent> UIAAdapter::GetCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph&) {
    std::vector<Intent> intents;

    for (const auto& element : snapshot.uiElements) {
        if (element.supportsInvoke && element.controlType == UiControlType::Button) {
            intents.push_back(BuildIntentFromElement(element, snapshot, IntentAction::Activate));
        }

        if (element.supportsValue && element.controlType == UiControlType::TextBox) {
            intents.push_back(BuildIntentFromElement(element, snapshot, IntentAction::SetValue));
        }

        if (element.supportsSelection || element.controlType == UiControlType::Menu ||
            element.controlType == UiControlType::MenuItem || element.controlType == UiControlType::ComboBox ||
            element.controlType == UiControlType::ListItem) {
            intents.push_back(BuildIntentFromElement(element, snapshot, IntentAction::Select));
        }
    }

    return intents;
}

bool UIAAdapter::CanExecute(const Intent& intent) const {
    if (intent.target.type != TargetType::UiElement) {
        return false;
    }

    return intent.action == IntentAction::Activate || intent.action == IntentAction::SetValue ||
        intent.action == IntentAction::Select;
}

ExecutionResult UIAAdapter::Execute(const Intent& intent) {
    const auto start = std::chrono::steady_clock::now();

    ExecutionResult result;
    result.method = "uia";

    const auto finalize = [&start](ExecutionResult* value) {
        const auto end = std::chrono::steady_clock::now();
        value->duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    };

    const HWND activeWindow = intent.context.activeWindow != nullptr ? intent.context.activeWindow : GetForegroundWindow();
    if (!activeWindow) {
        result.status = ExecutionStatus::FAILED;
        result.message = "No active window available for UI execution";
        finalize(&result);
        return result;
    }

    bool operation = false;
    const std::wstring primaryLabel = !intent.target.label.empty() ? intent.target.label : intent.target.automationId;

    switch (intent.action) {
    case IntentAction::Activate:
        operation = accessibilityLayer_.Activate(activeWindow, primaryLabel);
        if (!operation && !intent.target.automationId.empty() && intent.target.automationId != primaryLabel) {
            operation = accessibilityLayer_.Activate(activeWindow, intent.target.automationId);
            result.message = "Primary target failed; automationId fallback used";
        }
        break;
    case IntentAction::SetValue: {
        const std::wstring value = intent.params.Get("value");
        if (value.empty()) {
            result.status = ExecutionStatus::FAILED;
            result.message = "Missing parameter: value";
            finalize(&result);
            return result;
        }

        operation = accessibilityLayer_.SetValue(activeWindow, primaryLabel, value);
        if (!operation && !intent.target.automationId.empty() && intent.target.automationId != primaryLabel) {
            operation = accessibilityLayer_.SetValue(activeWindow, intent.target.automationId, value);
            result.message = "Primary target failed; automationId fallback used";
        }
        break;
    }
    case IntentAction::Select:
        operation = accessibilityLayer_.Select(activeWindow, primaryLabel);
        if (!operation && !intent.target.automationId.empty() && intent.target.automationId != primaryLabel) {
            operation = accessibilityLayer_.Select(activeWindow, intent.target.automationId);
            result.message = "Primary target failed; automationId fallback used";
        }
        break;
    default:
        result.status = ExecutionStatus::FAILED;
        result.message = "Unsupported UIA action";
        finalize(&result);
        return result;
    }

    bool verified = false;
    if (operation) {
        switch (intent.action) {
        case IntentAction::SetValue: {
            const auto element = accessibilityLayer_.FindElementByLabel(activeWindow, primaryLabel);
            verified = element.has_value() && element->value == intent.params.Get("value");
            break;
        }
        case IntentAction::Activate:
        case IntentAction::Select: {
            const auto element = accessibilityLayer_.FindElementByLabel(activeWindow, primaryLabel);
            verified = element.has_value();
            break;
        }
        default:
            verified = operation;
            break;
        }
    }

    result.verified = verified;
    result.status = operation ? (verified ? ExecutionStatus::SUCCESS : ExecutionStatus::PARTIAL)
                              : ExecutionStatus::FAILED;

    if (result.message.empty()) {
        if (result.status == ExecutionStatus::SUCCESS) {
            result.message = "UI action executed and verified";
        } else if (result.status == ExecutionStatus::PARTIAL) {
            result.message = "UI action executed; verification partial";
        } else {
            result.message = "UI action failed";
        }
    }

    finalize(&result);
    return result;
}

AdapterScore UIAAdapter::GetScore() const {
    AdapterScore score;
    score.reliability = 0.84F;
    score.latency = 60.0F;
    score.confidence = 0.90F;
    return score;
}

Intent UIAAdapter::BuildIntentFromElement(const UiElement& element, const ObserverSnapshot& snapshot, IntentAction action) const {
    Intent intent;
    intent.action = action;
    intent.name = ToString(action);
    intent.target = BuildTargetFromElement(element);
    intent.context = BuildContext(snapshot);
    intent.confidence = (action == IntentAction::Activate) ? 0.99F : (action == IntentAction::SetValue ? 0.98F : 0.96F);
    intent.source = "uia";
    intent.id = "uia:" + element.id + ":" + ToString(action);

    if (action == IntentAction::SetValue) {
        intent.params.values["value"] = L"";
    }

    return intent;
}

std::string FileSystemAdapter::Name() const {
    return "FileSystemAdapter";
}

std::vector<Intent> FileSystemAdapter::GetCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph&) {
    std::vector<Intent> intents;

    Intent createIntent;
    createIntent.action = IntentAction::Create;
    createIntent.name = "create";
    createIntent.target.type = TargetType::FileSystemPath;
    createIntent.target.path = std::filesystem::current_path().wstring();
    createIntent.target.label = L"filesystem";
    createIntent.context = BuildContext(snapshot);
    createIntent.confidence = 1.0F;
    createIntent.source = "filesystem";
    createIntent.id = "fs:create";
    createIntent.params.values["path"] = L"";
    intents.push_back(std::move(createIntent));

    for (const auto& entry : snapshot.fileSystemEntries) {
        if (entry.isDirectory) {
            continue;
        }

        intents.push_back(BuildIntentFromPath(snapshot, IntentAction::Delete, entry.path));
        intents.push_back(BuildIntentFromPath(snapshot, IntentAction::Move, entry.path));
    }

    return intents;
}

bool FileSystemAdapter::CanExecute(const Intent& intent) const {
    if (intent.target.type != TargetType::FileSystemPath) {
        return false;
    }

    return intent.action == IntentAction::Create || intent.action == IntentAction::Delete ||
        intent.action == IntentAction::Move;
}

ExecutionResult FileSystemAdapter::Execute(const Intent& intent) {
    const auto start = std::chrono::steady_clock::now();

    ExecutionResult result;
    result.method = "filesystem";

    std::error_code ec;
    switch (intent.action) {
    case IntentAction::Create: {
        std::filesystem::path path(intent.params.Get("path", intent.target.path));
        if (path.empty()) {
            result.status = ExecutionStatus::FAILED;
            result.message = "Missing parameter: path";
            break;
        }

        if (path.has_extension()) {
            std::ofstream output(path, std::ios::app);
            output.close();
            result.verified = std::filesystem::exists(path, ec);
        } else {
            const bool created = std::filesystem::create_directories(path, ec);
            result.verified = created || std::filesystem::exists(path, ec);
        }

        result.status = result.verified ? ExecutionStatus::SUCCESS : ExecutionStatus::FAILED;
        result.message = result.verified ? "Create executed" : "Create failed";
        break;
    }
    case IntentAction::Delete: {
        std::filesystem::path path(intent.params.Get("path", intent.target.path));
        if (path.empty()) {
            result.status = ExecutionStatus::FAILED;
            result.message = "Missing parameter: path";
            break;
        }

        const bool removed = std::filesystem::remove(path, ec);
        result.verified = removed || !std::filesystem::exists(path, ec);
        result.status = result.verified ? ExecutionStatus::SUCCESS : ExecutionStatus::FAILED;
        result.message = result.verified ? "Delete executed" : "Delete failed";
        break;
    }
    case IntentAction::Move: {
        std::filesystem::path sourcePath(intent.params.Get("path", intent.target.path));
        std::filesystem::path destinationPath(intent.params.Get("destination"));

        if (sourcePath.empty() || destinationPath.empty()) {
            result.status = ExecutionStatus::FAILED;
            result.message = "Missing parameters: path and/or destination";
            break;
        }

        if (std::filesystem::is_directory(destinationPath, ec)) {
            destinationPath /= sourcePath.filename();
        }

        const auto parent = destinationPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            ec.clear();
        }

        std::filesystem::rename(sourcePath, destinationPath, ec);
        result.verified = !ec && std::filesystem::exists(destinationPath) && !std::filesystem::exists(sourcePath);
        result.status = result.verified ? ExecutionStatus::SUCCESS : ExecutionStatus::FAILED;
        result.message = result.verified ? "Move executed" : "Move failed";
        break;
    }
    default:
        result.status = ExecutionStatus::FAILED;
        result.message = "Unsupported filesystem action";
        break;
    }

    if (result.status == ExecutionStatus::FAILED && ec) {
        result.message += ": " + ec.message();
    }

    const auto end = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    return result;
}

AdapterScore FileSystemAdapter::GetScore() const {
    AdapterScore score;
    score.reliability = 0.97F;
    score.latency = 10.0F;
    score.confidence = 0.98F;
    return score;
}

Intent FileSystemAdapter::BuildIntentFromPath(const ObserverSnapshot& snapshot, IntentAction action, const std::wstring& path) const {
    Intent intent;
    intent.action = action;
    intent.name = ToString(action);
    intent.target.type = TargetType::FileSystemPath;
    intent.target.path = path;
    intent.target.label = path;
    intent.context = BuildContext(snapshot);
    intent.confidence = 1.0F;
    intent.source = "filesystem";
    intent.id = "fs:" + Narrow(path) + ":" + ToString(action);

    intent.params.values["path"] = path;
    if (action == IntentAction::Move) {
        intent.params.values["destination"] = L"";
    }

    return intent;
}

void AdapterRegistry::Register(std::unique_ptr<Adapter> adapter) {
    if (!adapter) {
        return;
    }

    RegisterAdapter(std::shared_ptr<Adapter>(std::move(adapter)));
}

void AdapterRegistry::RegisterAdapter(std::shared_ptr<Adapter> adapter) {
    if (!adapter) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);

    AdapterRuntime runtime;
    const AdapterScore baseline = adapter->GetScore();
    runtime.reliabilityEma = Clamp01(baseline.reliability);
    runtime.latencyEmaMs = ClampMin(baseline.latency, 1.0F);
    runtime.confidence = Clamp01(baseline.confidence);
    runtime.registrationOrder = adapters_.size();
    runtime.lastUpdated = std::chrono::steady_clock::now();

    runtimeByAdapter_[adapter.get()] = runtime;
    adapters_.push_back(std::move(adapter));
}

std::vector<Adapter*> AdapterRegistry::GetAll() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<Adapter*> result;
    result.reserve(adapters_.size());

    for (const auto& adapter : adapters_) {
        result.push_back(adapter.get());
    }

    return result;
}

std::vector<std::shared_ptr<Adapter>> AdapterRegistry::GetAdapters() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return adapters_;
}

Adapter* AdapterRegistry::Resolve(const Intent& intent) const {
    return ResolveBest(intent).get();
}

std::shared_ptr<Adapter> AdapterRegistry::ResolveBest(const Intent& intent) const {
    return ResolveBest(intent, nullptr);
}

std::shared_ptr<Adapter> AdapterRegistry::ResolveBest(const Intent& intent, AdapterDecision* decision) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::shared_ptr<Adapter> best;
    float bestScore = -std::numeric_limits<float>::infinity();
    std::size_t bestOrder = std::numeric_limits<std::size_t>::max();

    if (decision != nullptr) {
        decision->candidates.clear();
        decision->selectedAdapter.clear();
        decision->usedFastPath = false;
    }

    for (const auto& adapter : adapters_) {
        AdapterDecisionCandidate candidate;
        candidate.adapterName = adapter->Name();
        candidate.matched = adapter->CanExecute(intent);

        const auto runtimeIt = runtimeByAdapter_.find(adapter.get());
        if (runtimeIt != runtimeByAdapter_.end()) {
            candidate.score = ComputeDecayedScore(*adapter, runtimeIt->second);
            candidate.finalScore = CompositeScore(candidate.score);

            if (candidate.matched) {
                const bool betterScore = candidate.finalScore > bestScore + 0.0001F;
                const bool tieBreak =
                    std::abs(candidate.finalScore - bestScore) <= 0.0001F &&
                    runtimeIt->second.registrationOrder < bestOrder;
                if (betterScore || tieBreak || best == nullptr) {
                    bestScore = candidate.finalScore;
                    bestOrder = runtimeIt->second.registrationOrder;
                    best = adapter;
                }
            }
        } else {
            candidate.score = adapter->GetScore();
            candidate.finalScore = CompositeScore(candidate.score);

            if (candidate.matched && (best == nullptr || candidate.finalScore > bestScore + 0.0001F)) {
                bestScore = candidate.finalScore;
                best = adapter;
            }
        }

        if (decision != nullptr) {
            decision->candidates.push_back(candidate);
        }
    }

    if (decision != nullptr) {
        std::sort(
            decision->candidates.begin(),
            decision->candidates.end(),
            [](const AdapterDecisionCandidate& left, const AdapterDecisionCandidate& right) {
                if (left.matched != right.matched) {
                    return left.matched > right.matched;
                }
                if (std::abs(left.finalScore - right.finalScore) > 0.0001F) {
                    return left.finalScore > right.finalScore;
                }
                return left.adapterName < right.adapterName;
            });

        if (best != nullptr) {
            decision->selectedAdapter = best->Name();
        }
    }

    if (decision != nullptr && decision->candidates.size() > 1U && !decision->selectedAdapter.empty()) {
        Logger::Info(
            "AdapterRegistry",
            "ResolveBest selected " + decision->selectedAdapter + " for action " + ToString(intent.action));
    }

    return best;
}

void AdapterRegistry::RecordExecution(const Adapter& adapter, const ExecutionResult& result) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const auto it = runtimeByAdapter_.find(&adapter);
    if (it == runtimeByAdapter_.end()) {
        return;
    }

    AdapterRuntime& runtime = it->second;

    const bool success = result.IsSuccess();
    if (success) {
        ++runtime.successCount;
    } else {
        ++runtime.failureCount;
    }

    constexpr float kReliabilityAlpha = 0.25F;
    const float successSignal = success ? 1.0F : 0.0F;
    runtime.reliabilityEma =
        (kReliabilityAlpha * successSignal) +
        ((1.0F - kReliabilityAlpha) * runtime.reliabilityEma);

    if (result.duration.count() > 0) {
        constexpr float kLatencyAlpha = 0.20F;
        const float latencyMs = static_cast<float>(result.duration.count());
        runtime.latencyEmaMs =
            (kLatencyAlpha * latencyMs) +
            ((1.0F - kLatencyAlpha) * runtime.latencyEmaMs);
    }

    const AdapterScore baseline = adapter.GetScore();
    runtime.confidence =
        (0.15F * Clamp01(baseline.confidence)) +
        (0.85F * runtime.confidence);

    runtime.lastUpdated = std::chrono::steady_clock::now();
}

float AdapterRegistry::Clamp01(float value) {
    if (value < 0.0F) {
        return 0.0F;
    }
    if (value > 1.0F) {
        return 1.0F;
    }
    return value;
}

float AdapterRegistry::ClampMin(float value, float minimum) {
    if (value < minimum) {
        return minimum;
    }
    return value;
}

AdapterScore AdapterRegistry::ComputeDecayedScore(const Adapter& adapter, const AdapterRuntime& runtime) const {
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds =
        std::chrono::duration_cast<std::chrono::duration<float>>(now - runtime.lastUpdated).count();
    const float decay = std::exp(-0.05F * std::max(0.0F, elapsedSeconds));

    constexpr float kReliabilityBaseline = 0.50F;
    constexpr float kLatencyBaselineMs = 80.0F;

    const float decayedReliability =
        kReliabilityBaseline + ((runtime.reliabilityEma - kReliabilityBaseline) * decay);
    const float decayedLatency =
        kLatencyBaselineMs + ((runtime.latencyEmaMs - kLatencyBaselineMs) * decay);

    const AdapterScore baseline = adapter.GetScore();

    AdapterScore effective;
    effective.reliability =
        Clamp01((0.70F * decayedReliability) + (0.30F * Clamp01(baseline.reliability)));
    effective.latency =
        ClampMin((0.70F * decayedLatency) + (0.30F * ClampMin(baseline.latency, 1.0F)), 1.0F);
    effective.confidence =
        Clamp01((0.60F * runtime.confidence) + (0.40F * Clamp01(baseline.confidence)));
    return effective;
}

}  // namespace iee