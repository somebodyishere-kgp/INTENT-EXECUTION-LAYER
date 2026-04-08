#include "Adapter.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <thread>

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

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
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
    context.snapshotVersion = snapshot.sequence;

    std::error_code ec;
    context.workingDirectory = std::filesystem::current_path(ec).wstring();
    return context;
}

std::wstring ToLowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool ContainsCaseInsensitive(const std::wstring& haystack, const std::wstring& needle) {
    if (haystack.empty() || needle.empty()) {
        return false;
    }

    return ToLowerWide(haystack).find(ToLowerWide(needle)) != std::wstring::npos;
}

std::wstring PrimaryTarget(const Intent& intent) {
    if (!intent.target.label.empty()) {
        return intent.target.label;
    }
    if (!intent.target.automationId.empty()) {
        return intent.target.automationId;
    }
    return intent.target.path;
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

AdapterMetadata Adapter::GetMetadata() const {
    AdapterMetadata metadata;
    metadata.name = Name();
    metadata.version = "1.0";

    const std::string lowerName = LowerAscii(Name());
    if (lowerName.find("filesystem") != std::string::npos) {
        metadata.priority = 10;
        metadata.supportedActions = {"create", "delete", "move"};
    } else if (lowerName.find("uia") != std::string::npos || lowerName.find("input") != std::string::npos) {
        metadata.priority = 50;
        metadata.supportedActions = {"activate", "select", "set_value"};
    } else {
        metadata.priority = 100;
    }

    std::sort(metadata.supportedActions.begin(), metadata.supportedActions.end());
    metadata.supportedActions.erase(
        std::unique(metadata.supportedActions.begin(), metadata.supportedActions.end()),
        metadata.supportedActions.end());

    return metadata;
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

VSCodeAdapter::VSCodeAdapter(IAccessibilityLayer& accessibilityLayer)
    : delegate_(accessibilityLayer) {}

std::string VSCodeAdapter::Name() const {
    return "VSCodeAdapter";
}

std::vector<Intent> VSCodeAdapter::GetCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph& graph) {
    if (!IsVsCodeSnapshot(snapshot)) {
        return {};
    }

    std::vector<Intent> intents = delegate_.GetCapabilities(snapshot, graph);
    for (Intent& intent : intents) {
        intent.source = "uia_vscode";
        intent.confidence = std::max(intent.confidence, 0.95F);

        if (!intent.id.empty()) {
            intent.id = "vscode:" + intent.id;
        }
    }

    return intents;
}

bool VSCodeAdapter::CanExecute(const Intent& intent) const {
    if (!delegate_.CanExecute(intent)) {
        return false;
    }

    if (IsVsCodeIntent(intent)) {
        return true;
    }

    return IsVsCodeTargetHint(PrimaryTarget(intent));
}

ExecutionResult VSCodeAdapter::Execute(const Intent& intent) {
    ExecutionResult result = delegate_.Execute(intent);
    result.method = "uia_vscode";
    return result;
}

AdapterScore VSCodeAdapter::GetScore() const {
    AdapterScore score;
    score.reliability = 0.90F;
    score.latency = 55.0F;
    score.confidence = 0.96F;
    return score;
}

bool VSCodeAdapter::IsVsCodeSnapshot(const ObserverSnapshot& snapshot) {
    return ContainsCaseInsensitive(snapshot.activeProcessPath, L"code.exe") ||
        ContainsCaseInsensitive(snapshot.activeProcessPath, L"code - insiders.exe") ||
        ContainsCaseInsensitive(snapshot.activeWindowTitle, L"visual studio code") ||
        ContainsCaseInsensitive(snapshot.activeWindowTitle, L"vscode") ||
        ContainsCaseInsensitive(snapshot.activeWindowTitle, L"vs code");
}

bool VSCodeAdapter::IsVsCodeIntent(const Intent& intent) {
    return ContainsCaseInsensitive(intent.context.application, L"code.exe") ||
        ContainsCaseInsensitive(intent.context.application, L"code - insiders.exe") ||
        ContainsCaseInsensitive(intent.context.windowTitle, L"visual studio code") ||
        ContainsCaseInsensitive(intent.context.windowTitle, L"vscode") ||
        ContainsCaseInsensitive(intent.context.windowTitle, L"vs code");
}

bool VSCodeAdapter::IsVsCodeTargetHint(const std::wstring& value) {
    if (value.empty()) {
        return false;
    }

    return ContainsCaseInsensitive(value, L"command palette") ||
        ContainsCaseInsensitive(value, L"explorer") ||
        ContainsCaseInsensitive(value, L"terminal") ||
        ContainsCaseInsensitive(value, L"extensions") ||
        ContainsCaseInsensitive(value, L"debug console") ||
        ContainsCaseInsensitive(value, L"source control") ||
        ContainsCaseInsensitive(value, L"problems") ||
        ContainsCaseInsensitive(value, L"editor");
}

std::string InputAdapter::Name() const {
    return "InputAdapter";
}

std::vector<Intent> InputAdapter::GetCapabilities(const ObserverSnapshot& snapshot, const CapabilityGraph&) {
    std::vector<Intent> intents;
    intents.reserve(snapshot.uiElements.size() * 2U);

    for (const auto& element : snapshot.uiElements) {
        if (element.controlType == UiControlType::Button || element.controlType == UiControlType::MenuItem ||
            element.controlType == UiControlType::ListItem) {
            Intent activate;
            activate.action = IntentAction::Activate;
            activate.name = ToString(IntentAction::Activate);
            activate.target = BuildTargetFromElement(element);
            activate.context = BuildContext(snapshot);
            activate.confidence = 0.60F;
            activate.source = "input";
            activate.id = "input:" + element.id + ":activate";
            intents.push_back(std::move(activate));
        }

        if (element.controlType == UiControlType::TextBox || element.supportsValue) {
            Intent setValue;
            setValue.action = IntentAction::SetValue;
            setValue.name = ToString(IntentAction::SetValue);
            setValue.target = BuildTargetFromElement(element);
            setValue.context = BuildContext(snapshot);
            setValue.confidence = 0.55F;
            setValue.source = "input";
            setValue.id = "input:" + element.id + ":set_value";
            setValue.params.values["value"] = L"";
            intents.push_back(std::move(setValue));
        }
    }

    return intents;
}

bool InputAdapter::CanExecute(const Intent& intent) const {
    if (intent.target.type != TargetType::UiElement) {
        return false;
    }

    return intent.action == IntentAction::Activate || intent.action == IntentAction::SetValue ||
        intent.action == IntentAction::Select;
}

ExecutionResult InputAdapter::Execute(const Intent& intent) {
    const auto start = std::chrono::steady_clock::now();

    ExecutionResult result;
    result.method = "input";

    const auto finalize = [&start](ExecutionResult* value) {
        const auto end = std::chrono::steady_clock::now();
        value->duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    };

    const HWND activeWindow = intent.context.activeWindow != nullptr ? intent.context.activeWindow : GetForegroundWindow();
    if (activeWindow == nullptr) {
        result.status = ExecutionStatus::FAILED;
        result.message = "No active window available for input simulation";
        result.verified = false;
        finalize(&result);
        return result;
    }

    const int configuredDelayMs = ReadTimingParamMs(intent, "delay_ms", 0, 5000);
    const int sequenceDelayMs = ReadTimingParamMs(intent, "sequence_ms", 0, 5000);
    const int holdMs = ReadTimingParamMs(intent, "hold_ms", 0, 2000);
    const int effectiveDelayMs = std::max(configuredDelayMs, sequenceDelayMs);

    if (effectiveDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(effectiveDelayMs));
    }

    SetForegroundWindow(activeWindow);

    bool ok = false;
    switch (intent.action) {
    case IntentAction::Activate:
    case IntentAction::Select:
        if (intent.target.screenCenter.x != 0 || intent.target.screenCenter.y != 0) {
            ok = SendLeftClick(intent.target.screenCenter.x, intent.target.screenCenter.y, holdMs);
        } else {
            ok = SendVirtualKey(VK_RETURN, holdMs);
        }
        result.verified = ok;
        result.status = ok ? ExecutionStatus::PARTIAL : ExecutionStatus::FAILED;
        result.message = ok ? "Input action sent" : "Input action failed";
        break;
    case IntentAction::SetValue: {
        const std::wstring value = intent.params.Get("value");
        if (value.empty()) {
            result.status = ExecutionStatus::FAILED;
            result.verified = false;
            result.message = "Missing parameter: value";
            finalize(&result);
            return result;
        }

        bool focused = true;
        if (intent.target.screenCenter.x != 0 || intent.target.screenCenter.y != 0) {
            focused = SendLeftClick(intent.target.screenCenter.x, intent.target.screenCenter.y, holdMs);
        }

        ok = focused && SendUnicodeText(value, holdMs);
        result.verified = ok;
        result.status = ok ? ExecutionStatus::PARTIAL : ExecutionStatus::FAILED;
        result.message = ok ? "Text input sent" : "Text input failed";
        break;
    }
    default:
        result.status = ExecutionStatus::FAILED;
        result.verified = false;
        result.message = "Unsupported action for input adapter";
        break;
    }

    finalize(&result);
    return result;
}

AdapterScore InputAdapter::GetScore() const {
    AdapterScore score;
    score.reliability = 0.58F;
    score.latency = 18.0F;
    score.confidence = 0.42F;
    return score;
}

int InputAdapter::ReadTimingParamMs(
    const Intent& intent,
    std::string_view key,
    int defaultValue,
    int maxValue) {
    const std::wstring raw = intent.params.Get(key);
    if (raw.empty()) {
        return defaultValue;
    }

    const std::string narrow = Narrow(raw);
    if (narrow.empty()) {
        return defaultValue;
    }

    int parsed = defaultValue;
    const auto [ptr, error] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
    if (error != std::errc() || ptr != narrow.data() + narrow.size()) {
        return defaultValue;
    }

    if (parsed < 0) {
        return 0;
    }
    if (parsed > maxValue) {
        return maxValue;
    }
    return parsed;
}

bool InputAdapter::SendUnicodeText(const std::wstring& text, int holdMs) {
    for (const wchar_t ch : text) {
        INPUT keyDown{};
        keyDown.type = INPUT_KEYBOARD;
        keyDown.ki.wScan = ch;
        keyDown.ki.dwFlags = KEYEVENTF_UNICODE;

        INPUT keyUp{};
        keyUp.type = INPUT_KEYBOARD;
        keyUp.ki.wScan = ch;
        keyUp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        const UINT sentDown = SendInput(1, &keyDown, sizeof(INPUT));
        if (sentDown != 1) {
            return false;
        }

        if (holdMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
        }

        const UINT sentUp = SendInput(1, &keyUp, sizeof(INPUT));
        if (sentUp != 1) {
            return false;
        }
    }

    return true;
}

bool InputAdapter::SendLeftClick(int x, int y, int holdMs) {
    const int screenWidth = std::max(1, GetSystemMetrics(SM_CXSCREEN) - 1);
    const int screenHeight = std::max(1, GetSystemMetrics(SM_CYSCREEN) - 1);

    const LONG normalizedX = static_cast<LONG>((static_cast<double>(x) * 65535.0) / static_cast<double>(screenWidth));
    const LONG normalizedY = static_cast<LONG>((static_cast<double>(y) * 65535.0) / static_cast<double>(screenHeight));

    INPUT move{};
    move.type = INPUT_MOUSE;
    move.mi.dx = normalizedX;
    move.mi.dy = normalizedY;
    move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

    INPUT down{};
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

    INPUT up{};
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = MOUSEEVENTF_LEFTUP;

    const UINT sentMove = SendInput(1, &move, sizeof(INPUT));
    const UINT sentDown = SendInput(1, &down, sizeof(INPUT));
    if (sentMove != 1 || sentDown != 1) {
        return false;
    }

    if (holdMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
    }

    const UINT sentUp = SendInput(1, &up, sizeof(INPUT));
    return sentUp == 1;
}

bool InputAdapter::SendVirtualKey(WORD keyCode, int holdMs) {
    INPUT keyDown{};
    keyDown.type = INPUT_KEYBOARD;
    keyDown.ki.wVk = keyCode;

    INPUT keyUp{};
    keyUp.type = INPUT_KEYBOARD;
    keyUp.ki.wVk = keyCode;
    keyUp.ki.dwFlags = KEYEVENTF_KEYUP;

    const UINT sentDown = SendInput(1, &keyDown, sizeof(INPUT));
    if (sentDown != 1) {
        return false;
    }

    if (holdMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
    }

    const UINT sentUp = SendInput(1, &keyUp, sizeof(INPUT));
    return sentUp == 1;
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

std::vector<AdapterMetadata> AdapterRegistry::ListMetadata() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<AdapterMetadata> metadata;
    metadata.reserve(adapters_.size());

    for (const auto& adapter : adapters_) {
        if (adapter == nullptr) {
            continue;
        }
        metadata.push_back(adapter->GetMetadata());
    }

    std::sort(metadata.begin(), metadata.end(), [](const AdapterMetadata& left, const AdapterMetadata& right) {
        if (left.priority != right.priority) {
            return left.priority < right.priority;
        }
        return left.name < right.name;
    });

    return metadata;
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