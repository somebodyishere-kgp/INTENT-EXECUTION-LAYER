#pragma once

#include <Windows.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iee {

enum class IntentAction {
    Activate,
    SetValue,
    Select,
    Create,
    Delete,
    Move,
    Unknown
};

enum class TargetType {
    UiElement,
    FileSystemPath,
    Unknown
};

enum class ExecutionStatus {
    SUCCESS,
    FAILED,
    PARTIAL
};

struct Target {
    TargetType type{TargetType::Unknown};
    std::wstring label;
    std::wstring automationId;
    std::wstring path;
    std::string nodeId;
    int hierarchyDepth{0};
    POINT screenCenter{0, 0};
    bool focused{false};
};

struct Params {
    std::map<std::string, std::wstring> values;

    bool Has(std::string_view key) const;
    std::wstring Get(std::string_view key, const std::wstring& defaultValue = L"") const;
};

struct Context {
    std::wstring application;
    std::wstring windowTitle;
    std::wstring workingDirectory;
    POINT cursor{0, 0};
    HWND activeWindow{nullptr};
    std::uint64_t snapshotTicks{0};
    std::uint64_t snapshotVersion{0};
    std::uint64_t controlFrame{0};
};

struct Constraints {
    int timeoutMs{2500};
    int maxRetries{2};
    bool requiresVerification{true};
    bool allowFallback{true};
};

std::string ToString(IntentAction action);
IntentAction IntentActionFromString(std::string_view value);
std::string ToString(ExecutionStatus status);

struct Intent {
    std::string id;
    std::string name;
    IntentAction action{IntentAction::Unknown};
    Target target;
    Params params;
    Context context;
    Constraints constraints;
    float confidence{0.0F};
    std::string source;

    bool IsValid(std::string* error = nullptr) const;
    std::string Serialize() const;
    static std::optional<Intent> Deserialize(std::string_view payload);
};

class IntentValidator {
public:
    bool Validate(const Intent& intent, std::string* error = nullptr) const;
};

}  // namespace iee
