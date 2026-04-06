#include "Intent.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <optional>
#include <sstream>

namespace iee {
namespace {

std::string NormalizeAsciiLower(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());

    for (const char ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            normalized.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            normalized.push_back(ch);
        }
    }

    return normalized;
}

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

std::wstring Wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int requiredChars = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (requiredChars <= 1) {
        return L"";
    }

    std::wstring result(static_cast<std::size_t>(requiredChars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), requiredChars);
    result.pop_back();
    return result;
}

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8U);

    for (const char ch : value) {
        switch (ch) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

std::optional<std::string> ExtractJsonString(std::string_view payload, std::string_view key) {
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    const std::size_t keyPos = payload.find(pattern);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t colonPos = payload.find(':', keyPos + pattern.size());
    if (colonPos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t firstQuote = payload.find('"', colonPos + 1U);
    if (firstQuote == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t secondQuote = payload.find('"', firstQuote + 1U);
    if (secondQuote == std::string_view::npos || secondQuote <= firstQuote) {
        return std::nullopt;
    }

    return std::string(payload.substr(firstQuote + 1U, secondQuote - firstQuote - 1U));
}

std::optional<float> ExtractJsonFloat(std::string_view payload, std::string_view key) {
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    const std::size_t keyPos = payload.find(pattern);
    if (keyPos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t colonPos = payload.find(':', keyPos + pattern.size());
    if (colonPos == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t start = payload.find_first_not_of(" \t\n\r", colonPos + 1U);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }

    std::size_t end = start;
    while (end < payload.size()) {
        const char ch = payload[end];
        if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.') {
            ++end;
            continue;
        }
        break;
    }

    if (end <= start) {
        return std::nullopt;
    }

    const std::string token(payload.substr(start, end - start));
    try {
        return std::stof(token);
    } catch (...) {
        return std::nullopt;
    }
}

TargetType TargetTypeFromAction(IntentAction action) {
    switch (action) {
    case IntentAction::Activate:
    case IntentAction::SetValue:
    case IntentAction::Select:
        return TargetType::UiElement;
    case IntentAction::Create:
    case IntentAction::Delete:
    case IntentAction::Move:
        return TargetType::FileSystemPath;
    default:
        return TargetType::Unknown;
    }
}

}  // namespace

bool Params::Has(std::string_view key) const {
    return values.find(std::string(key)) != values.end();
}

std::wstring Params::Get(std::string_view key, const std::wstring& defaultValue) const {
    const auto it = values.find(std::string(key));
    if (it == values.end()) {
        return defaultValue;
    }

    return it->second;
}

std::string ToString(IntentAction action) {
    switch (action) {
    case IntentAction::Activate:
        return "activate";
    case IntentAction::SetValue:
        return "set_value";
    case IntentAction::Select:
        return "select";
    case IntentAction::Create:
        return "create";
    case IntentAction::Delete:
        return "delete";
    case IntentAction::Move:
        return "move";
    case IntentAction::Unknown:
    default:
        return "unknown";
    }
}

IntentAction IntentActionFromString(std::string_view value) {
    const std::string normalized = NormalizeAsciiLower(value);

    if (normalized == "activate") {
        return IntentAction::Activate;
    }
    if (normalized == "set_value") {
        return IntentAction::SetValue;
    }
    if (normalized == "select") {
        return IntentAction::Select;
    }
    if (normalized == "create") {
        return IntentAction::Create;
    }
    if (normalized == "delete") {
        return IntentAction::Delete;
    }
    if (normalized == "move") {
        return IntentAction::Move;
    }

    return IntentAction::Unknown;
}

std::string ToString(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::SUCCESS:
        return "SUCCESS";
    case ExecutionStatus::FAILED:
        return "FAILED";
    case ExecutionStatus::PARTIAL:
        return "PARTIAL";
    }

    return "FAILED";
}

bool Intent::IsValid(std::string* error) const {
    auto fail = [error](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (action == IntentAction::Unknown) {
        return fail("Intent action is unknown");
    }

    if (confidence < 0.0F || confidence > 1.0F) {
        return fail("Intent confidence must be in range [0,1]");
    }

    if (target.type == TargetType::Unknown) {
        return fail("Intent target type is unknown");
    }

    if (target.type == TargetType::UiElement && target.label.empty() && target.automationId.empty()) {
        return fail("UI intent requires target label or automation id");
    }

    if (target.type == TargetType::FileSystemPath && target.path.empty()) {
        return fail("Filesystem intent requires a target path");
    }

    if (action == IntentAction::SetValue && !params.Has("value")) {
        return fail("set_value intent requires parameter 'value'");
    }

    if (action == IntentAction::Move && (!params.Has("path") || !params.Has("destination"))) {
        return fail("move intent requires parameters 'path' and 'destination'");
    }

    if (constraints.maxRetries < 0) {
        return fail("maxRetries cannot be negative");
    }

    if (constraints.timeoutMs <= 0) {
        return fail("timeoutMs must be positive");
    }

    return true;
}

std::string Intent::Serialize() const {
    std::ostringstream stream;
    stream << '{';
    stream << "\"id\":\"" << EscapeJson(id) << "\",";
    stream << "\"name\":\"" << EscapeJson(name.empty() ? ToString(action) : name) << "\",";
    stream << "\"action\":\"" << EscapeJson(ToString(action)) << "\",";
    stream << "\"source\":\"" << EscapeJson(source) << "\",";
    stream << "\"confidence\":" << confidence << ',';
    stream << "\"target\":{";
    stream << "\"type\":\"" << (target.type == TargetType::UiElement ? "ui" : (target.type == TargetType::FileSystemPath ? "fs" : "unknown")) << "\",";
    stream << "\"label\":\"" << EscapeJson(Narrow(target.label)) << "\",";
    stream << "\"automationId\":\"" << EscapeJson(Narrow(target.automationId)) << "\",";
    stream << "\"path\":\"" << EscapeJson(Narrow(target.path)) << "\",";
    stream << "\"nodeId\":\"" << EscapeJson(target.nodeId) << "\",";
    stream << "\"depth\":" << target.hierarchyDepth << ',';
    stream << "\"focused\":" << (target.focused ? "true" : "false") << ',';
    stream << "\"x\":" << target.screenCenter.x << ',';
    stream << "\"y\":" << target.screenCenter.y;
    stream << "},\"params\":{";

    bool first = true;
    for (const auto& entry : params.values) {
        if (!first) {
            stream << ',';
        }
        first = false;
        stream << "\"" << EscapeJson(entry.first) << "\":\"" << EscapeJson(Narrow(entry.second)) << "\"";
    }

    stream << "},\"constraints\":{";
    stream << "\"timeoutMs\":" << constraints.timeoutMs << ',';
    stream << "\"maxRetries\":" << constraints.maxRetries << ',';
    stream << "\"requiresVerification\":" << (constraints.requiresVerification ? "true" : "false") << ',';
    stream << "\"allowFallback\":" << (constraints.allowFallback ? "true" : "false");
    stream << "}}";
    return stream.str();
}

std::optional<Intent> Intent::Deserialize(std::string_view payload) {
    Intent intent;

    const auto actionToken = ExtractJsonString(payload, "action");
    if (!actionToken.has_value()) {
        return std::nullopt;
    }

    intent.action = IntentActionFromString(*actionToken);
    intent.name = ExtractJsonString(payload, "name").value_or(ToString(intent.action));
    intent.id = ExtractJsonString(payload, "id").value_or("");
    intent.source = ExtractJsonString(payload, "source").value_or("api");
    intent.confidence = ExtractJsonFloat(payload, "confidence").value_or(1.0F);

    intent.target.label = Wide(ExtractJsonString(payload, "label").value_or(""));
    intent.target.automationId = Wide(ExtractJsonString(payload, "automationId").value_or(""));
    intent.target.path = Wide(ExtractJsonString(payload, "path").value_or(""));
    intent.target.nodeId = ExtractJsonString(payload, "nodeId").value_or("");
    intent.target.type = TargetTypeFromAction(intent.action);

    const auto value = ExtractJsonString(payload, "value");
    if (value.has_value()) {
        intent.params.values["value"] = Wide(*value);
    }

    const auto path = ExtractJsonString(payload, "path");
    if (path.has_value()) {
        intent.params.values["path"] = Wide(*path);
    }

    const auto destination = ExtractJsonString(payload, "destination");
    if (destination.has_value()) {
        intent.params.values["destination"] = Wide(*destination);
    }

    std::string validationError;
    if (!intent.IsValid(&validationError)) {
        return std::nullopt;
    }

    return intent;
}

bool IntentValidator::Validate(const Intent& intent, std::string* error) const {
    return intent.IsValid(error);
}

}  // namespace iee
