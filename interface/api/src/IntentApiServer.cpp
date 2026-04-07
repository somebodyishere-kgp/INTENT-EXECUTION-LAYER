#include <WinSock2.h>
#include <WS2tcpip.h>

#include "IntentApiServer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "ActionSequence.h"

#pragma comment(lib, "Ws2_32.lib")

namespace iee {
namespace {

std::wstring Wide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (required <= 1) {
        return L"";
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), required);
    wide.pop_back();
    return wide;
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

std::string EscapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 16U);

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

std::string ToAsciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
}

std::string BuildResponse(int statusCode, const std::string& statusText, const std::string& body) {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << statusCode << ' ' << statusText << "\r\n";
    stream << "Content-Type: application/json\r\n";
    stream << "Content-Length: " << body.size() << "\r\n";
    stream << "Connection: close\r\n\r\n";
    stream << body;
    return stream.str();
}

std::string BuildErrorBody(const std::string& code, const std::string& message) {
    std::ostringstream stream;
    stream << "{\"error\":{";
    stream << "\"code\":\"" << EscapeJson(code) << "\",";
    stream << "\"message\":\"" << EscapeJson(message) << "\"";
    stream << "}}";
    return stream.str();
}

std::string BuildErrorResponse(int statusCode, const std::string& statusText, const std::string& code, const std::string& message) {
    return BuildResponse(statusCode, statusText, BuildErrorBody(code, message));
}

std::size_t ParseContentLength(std::string_view requestHead) {
    const std::string lowerHead = ToAsciiLower(std::string(requestHead));
    const std::string marker = "content-length:";
    const std::size_t markerPos = lowerHead.find(marker);
    if (markerPos == std::string::npos) {
        return 0;
    }

    std::size_t valueStart = markerPos + marker.size();
    while (valueStart < lowerHead.size() && (lowerHead[valueStart] == ' ' || lowerHead[valueStart] == '\t')) {
        ++valueStart;
    }

    std::size_t valueEnd = valueStart;
    while (valueEnd < lowerHead.size() && lowerHead[valueEnd] >= '0' && lowerHead[valueEnd] <= '9') {
        ++valueEnd;
    }

    if (valueEnd <= valueStart) {
        return 0;
    }

    std::size_t length = 0;
    const auto [ptr, error] = std::from_chars(
        lowerHead.data() + static_cast<std::ptrdiff_t>(valueStart),
        lowerHead.data() + static_cast<std::ptrdiff_t>(valueEnd),
        length);
    if (error != std::errc() || ptr != lowerHead.data() + static_cast<std::ptrdiff_t>(valueEnd)) {
        return 0;
    }

    return length;
}

void SkipWhitespace(std::string_view payload, std::size_t* index) {
    while (*index < payload.size()) {
        const char ch = payload[*index];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++(*index);
            continue;
        }
        break;
    }
}

bool ParseJsonString(std::string_view payload, std::size_t* index, std::string* output) {
    if (*index >= payload.size() || payload[*index] != '"') {
        return false;
    }

    ++(*index);
    std::string value;
    value.reserve(32U);

    while (*index < payload.size()) {
        const char ch = payload[*index];
        ++(*index);

        if (ch == '\\') {
            if (*index >= payload.size()) {
                return false;
            }

            const char escaped = payload[*index];
            ++(*index);
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                return false;
            }
            continue;
        }

        if (ch == '"') {
            *output = std::move(value);
            return true;
        }

        value.push_back(ch);
    }

    return false;
}

bool ParseFlatJsonObject(
    std::string_view payload,
    std::map<std::string, std::string>* values,
    std::string* errorMessage) {
    values->clear();

    std::size_t index = 0;
    SkipWhitespace(payload, &index);
    if (index >= payload.size() || payload[index] != '{') {
        if (errorMessage != nullptr) {
            *errorMessage = "JSON body must start with '{'";
        }
        return false;
    }
    ++index;

    while (true) {
        SkipWhitespace(payload, &index);
        if (index >= payload.size()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Unexpected end of JSON";
            }
            return false;
        }

        if (payload[index] == '}') {
            ++index;
            SkipWhitespace(payload, &index);
            if (index != payload.size()) {
                if (errorMessage != nullptr) {
                    *errorMessage = "Trailing characters after JSON object";
                }
                return false;
            }
            return true;
        }

        std::string key;
        if (!ParseJsonString(payload, &index, &key)) {
            if (errorMessage != nullptr) {
                *errorMessage = "Invalid JSON key";
            }
            return false;
        }

        SkipWhitespace(payload, &index);
        if (index >= payload.size() || payload[index] != ':') {
            if (errorMessage != nullptr) {
                *errorMessage = "Expected ':' after key";
            }
            return false;
        }
        ++index;

        SkipWhitespace(payload, &index);
        std::string value;
        if (!ParseJsonString(payload, &index, &value)) {
            if (errorMessage != nullptr) {
                *errorMessage = "Only string values are supported";
            }
            return false;
        }
        (*values)[key] = value;

        SkipWhitespace(payload, &index);
        if (index >= payload.size()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Unexpected end after value";
            }
            return false;
        }

        if (payload[index] == ',') {
            ++index;
            continue;
        }

        if (payload[index] == '}') {
            continue;
        }

        if (errorMessage != nullptr) {
            *errorMessage = "Expected ',' or '}'";
        }
        return false;
    }
}

bool IsUiAction(IntentAction action) {
    return action == IntentAction::Activate || action == IntentAction::SetValue || action == IntentAction::Select;
}

bool IsFileAction(IntentAction action) {
    return action == IntentAction::Create || action == IntentAction::Delete || action == IntentAction::Move;
}

std::string ReadPayloadValue(const std::map<std::string, std::string>& payload, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const auto it = payload.find(key);
        if (it != payload.end()) {
            return it->second;
        }
    }
    return "";
}

bool ParseInt32(const std::string& value, int* output) {
    if (value.empty() || output == nullptr) {
        return false;
    }

    int parsed = 0;
    const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc() || ptr != value.data() + value.size()) {
        return false;
    }

    *output = parsed;
    return true;
}

bool ParseUint64(const std::string& value, std::uint64_t* output) {
    if (value.empty() || output == nullptr) {
        return false;
    }

    std::uint64_t parsed = 0;
    const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc() || ptr != value.data() + value.size()) {
        return false;
    }

    *output = parsed;
    return true;
}

std::int64_t EpochMs(std::chrono::system_clock::time_point timePoint) {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count());
}

std::string SerializeRectJson(const RECT& rect) {
    std::ostringstream json;
    json << "{";
    json << "\"left\":" << rect.left << ",";
    json << "\"top\":" << rect.top << ",";
    json << "\"right\":" << rect.right << ",";
    json << "\"bottom\":" << rect.bottom;
    json << "}";
    return json.str();
}

std::string SerializeVisualElementJson(const VisualElement& element) {
    std::ostringstream json;
    json << "{";
    json << "\"id\":\"" << EscapeJson(element.id) << "\",";
    json << "\"kind\":\"" << EscapeJson(element.kind) << "\",";
    json << "\"bounds\":" << SerializeRectJson(element.bounds) << ",";
    json << "\"color_cluster\":" << element.colorCluster << ",";
    json << "\"edge_density\":" << element.edgeDensity << ",";
    json << "\"text_like\":" << (element.textLike ? "true" : "false") << ",";
    json << "\"confidence\":" << element.confidence;
    json << "}";
    return json.str();
}

std::string SerializeScreenElementJson(const ScreenElement& element) {
    std::ostringstream json;
    json << "{";
    json << "\"id\":\"" << EscapeJson(element.id) << "\",";
    json << "\"source\":\"" << EscapeJson(element.source) << "\",";
    json << "\"ui_element_id\":\"" << EscapeJson(element.uiElementId) << "\",";
    json << "\"label\":\"" << EscapeJson(Narrow(element.label)) << "\",";
    json << "\"bounds\":" << SerializeRectJson(element.bounds) << ",";
    json << "\"confidence\":" << element.confidence << ",";
    json << "\"focused\":" << (element.focused ? "true" : "false") << ",";
    json << "\"text_like\":" << (element.textLike ? "true" : "false");
    json << "}";
    return json.str();
}

std::string SerializeScreenStateJson(const ScreenState& state) {
    std::ostringstream json;
    json << "{";
    json << "\"frame_id\":" << state.frameId << ",";
    json << "\"environment_sequence\":" << state.environmentSequence << ",";
    json << "\"captured_at_ms\":" << EpochMs(state.capturedAt) << ",";
    json << "\"width\":" << state.width << ",";
    json << "\"height\":" << state.height << ",";
    json << "\"signature\":" << state.signature << ",";
    json << "\"simulated\":" << (state.simulated ? "true" : "false") << ",";
    json << "\"valid\":" << (state.valid ? "true" : "false") << ",";
    json << "\"cursor\":{";
    json << "\"x\":" << state.cursorPosition.x << ",";
    json << "\"y\":" << state.cursorPosition.y;
    json << "},";
    json << "\"visual_elements\":[";
    for (std::size_t index = 0; index < state.visualElements.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << SerializeVisualElementJson(state.visualElements[index]);
    }
    json << "],";
    json << "\"elements\":[";
    for (std::size_t index = 0; index < state.elements.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << SerializeScreenElementJson(state.elements[index]);
    }
    json << "]";
    json << "}";
    return json.str();
}

std::string SerializeUnifiedStateJson(const UnifiedState& state) {
    std::ostringstream json;
    json << "{";
    json << "\"frame_id\":" << state.frameId << ",";
    json << "\"environment_sequence\":" << state.environmentSequence << ",";
    json << "\"captured_at_ms\":" << EpochMs(state.capturedAt) << ",";
    json << "\"signature\":" << state.signature << ",";
    json << "\"valid\":" << (state.valid ? "true" : "false") << ",";
    json << "\"screen_state\":" << SerializeScreenStateJson(state.screenState) << ",";
    json << "\"interaction_graph\":" << InteractionGraphBuilder::SerializeGraphJson(state.interactionGraph);
    json << "}";
    return json.str();
}

bool ScreenElementEquivalent(const ScreenElement& left, const ScreenElement& right) {
    const auto sameRect = left.bounds.left == right.bounds.left &&
        left.bounds.top == right.bounds.top &&
        left.bounds.right == right.bounds.right &&
        left.bounds.bottom == right.bounds.bottom;

    return sameRect &&
        left.source == right.source &&
        left.uiElementId == right.uiElementId &&
        left.label == right.label &&
        std::abs(left.confidence - right.confidence) < 0.0001 &&
        left.focused == right.focused &&
        left.textLike == right.textLike;
}

std::string SerializeScreenDeltaJson(const ScreenState& base, const ScreenState& current, bool* changedOut) {
    std::unordered_map<std::string, ScreenElement> baseById;
    baseById.reserve(base.elements.size());
    for (const ScreenElement& element : base.elements) {
        baseById[element.id] = element;
    }

    std::unordered_map<std::string, ScreenElement> currentById;
    currentById.reserve(current.elements.size());
    for (const ScreenElement& element : current.elements) {
        currentById[element.id] = element;
    }

    std::vector<ScreenElement> added;
    std::vector<ScreenElement> updated;
    std::vector<std::string> removed;

    for (const auto& entry : currentById) {
        const auto it = baseById.find(entry.first);
        if (it == baseById.end()) {
            added.push_back(entry.second);
            continue;
        }

        if (!ScreenElementEquivalent(it->second, entry.second)) {
            updated.push_back(entry.second);
        }
    }

    for (const auto& entry : baseById) {
        if (currentById.find(entry.first) == currentById.end()) {
            removed.push_back(entry.first);
        }
    }

    const bool changed = !added.empty() || !updated.empty() || !removed.empty();
    if (changedOut != nullptr) {
        *changedOut = changed;
    }

    std::ostringstream json;
    json << "{";
    json << "\"changed\":" << (changed ? "true" : "false") << ",";
    json << "\"added\":[";
    for (std::size_t index = 0; index < added.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << SerializeScreenElementJson(added[index]);
    }
    json << "],";
    json << "\"updated\":[";
    for (std::size_t index = 0; index < updated.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << SerializeScreenElementJson(updated[index]);
    }
    json << "],";
    json << "\"removed\":[";
    for (std::size_t index = 0; index < removed.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "\"" << EscapeJson(removed[index]) << "\"";
    }
    json << "]";
    json << "}";
    return json.str();
}

std::string SerializeEnvironmentStateJson(const EnvironmentState& state) {
    std::ostringstream json;
    json << "{";
    json << "\"sequence\":" << state.sequence << ",";
    json << "\"source_snapshot_version\":" << state.sourceSnapshotVersion << ",";
    json << "\"captured_at_ms\":" << EpochMs(state.capturedAt) << ",";
    json << "\"active_window_title\":\"" << EscapeJson(Narrow(state.activeWindowTitle)) << "\",";
    json << "\"active_process_path\":\"" << EscapeJson(Narrow(state.activeProcessPath)) << "\",";
    json << "\"cursor\":{";
    json << "\"x\":" << state.cursorPosition.x << ",";
    json << "\"y\":" << state.cursorPosition.y;
    json << "},";
    json << "\"ui_element_count\":" << state.uiElements.size() << ",";
    json << "\"filesystem_entry_count\":" << state.fileSystemEntries.size() << ",";
    json << "\"simulated\":" << (state.simulated ? "true" : "false") << ",";
    json << "\"valid\":" << (state.valid ? "true" : "false") << ",";
    json << "\"perception\":{";
    json << "\"dominant_surface\":\"" << EscapeJson(state.perception.dominantSurface) << "\",";
    json << "\"focus_ratio\":" << state.perception.focusRatio << ",";
    json << "\"occupancy_ratio\":" << state.perception.occupancyRatio << ",";
    json << "\"ui_signature\":" << state.perception.uiSignature << ",";
    json << "\"compute_ms\":" << state.perception.computeMs << ",";
    json << "\"regions\":[";

    for (std::size_t index = 0; index < state.perception.regions.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        const EnvironmentRegion& region = state.perception.regions[index];
        json << "{";
        json << "\"left\":" << region.bounds.left << ",";
        json << "\"top\":" << region.bounds.top << ",";
        json << "\"right\":" << region.bounds.right << ",";
        json << "\"bottom\":" << region.bounds.bottom << ",";
        json << "\"element_count\":" << region.elementCount << ",";
        json << "\"has_focus\":" << (region.hasFocus ? "true" : "false");
        json << "}";
    }

    json << "]";
    json << "},";
    json << "\"screen_state\":" << SerializeScreenStateJson(state.screenState) << ",";
    json << "\"unified_state\":" << SerializeUnifiedStateJson(state.unifiedState) << ",";
    json << "\"vision_timing\":{";
    json << "\"capture_ms\":" << state.visionTiming.captureMs << ",";
    json << "\"detection_ms\":" << state.visionTiming.detectionMs << ",";
    json << "\"merge_ms\":" << state.visionTiming.mergeMs << ",";
    json << "\"total_ms\":" << state.visionTiming.totalMs;
    json << "}";
    json << "}";
    return json.str();
}

bool BuildStreamIntent(
    const std::map<std::string, std::string>& payload,
    Intent* intent,
    std::string* errorCode,
    std::string* errorMessage) {
    if (intent == nullptr) {
        if (errorCode != nullptr) {
            *errorCode = "invalid_request";
        }
        if (errorMessage != nullptr) {
            *errorMessage = "Intent output is null";
        }
        return false;
    }

    const std::string actionRaw = ReadPayloadValue(payload, {"action"});
    const IntentAction action = IntentActionFromString(actionRaw);
    if (action == IntentAction::Unknown) {
        if (errorCode != nullptr) {
            *errorCode = actionRaw.empty() ? "missing_action" : "unknown_action";
        }
        if (errorMessage != nullptr) {
            *errorMessage = actionRaw.empty() ? "Missing required field: action" : "Unknown action";
        }
        return false;
    }

    Intent streamIntent;
    streamIntent.action = action;
    streamIntent.name = ToString(action);
    streamIntent.source = "stream";
    streamIntent.confidence = 1.0F;

    if (IsUiAction(action)) {
        const std::string target = ReadPayloadValue(payload, {"target"});
        if (target.empty()) {
            if (errorCode != nullptr) {
                *errorCode = "missing_target";
            }
            if (errorMessage != nullptr) {
                *errorMessage = "Missing required field: target";
            }
            return false;
        }

        streamIntent.target.type = TargetType::UiElement;
        streamIntent.target.label = Wide(target);

        if (action == IntentAction::SetValue) {
            const std::string value = ReadPayloadValue(payload, {"value"});
            if (value.empty()) {
                if (errorCode != nullptr) {
                    *errorCode = "missing_value";
                }
                if (errorMessage != nullptr) {
                    *errorMessage = "Missing required field: value";
                }
                return false;
            }
            streamIntent.params.values["value"] = Wide(value);
        }
    } else if (IsFileAction(action)) {
        const std::string path = ReadPayloadValue(payload, {"path"});
        if (path.empty()) {
            if (errorCode != nullptr) {
                *errorCode = "missing_path";
            }
            if (errorMessage != nullptr) {
                *errorMessage = "Missing required field: path";
            }
            return false;
        }

        streamIntent.target.type = TargetType::FileSystemPath;
        streamIntent.target.path = Wide(path);
        streamIntent.target.label = streamIntent.target.path;
        streamIntent.params.values["path"] = streamIntent.target.path;

        if (action == IntentAction::Move) {
            const std::string destination = ReadPayloadValue(payload, {"destination"});
            if (destination.empty()) {
                if (errorCode != nullptr) {
                    *errorCode = "missing_destination";
                }
                if (errorMessage != nullptr) {
                    *errorMessage = "Missing required field: destination";
                }
                return false;
            }

            streamIntent.params.values["destination"] = Wide(destination);
        }
    } else {
        if (errorCode != nullptr) {
            *errorCode = "unsupported_action";
        }
        if (errorMessage != nullptr) {
            *errorMessage = "Action is not executable";
        }
        return false;
    }

    int timeoutMs = 0;
    if (ParseInt32(ReadPayloadValue(payload, {"timeout_ms", "timeoutMs"}), &timeoutMs) && timeoutMs > 0) {
        streamIntent.constraints.timeoutMs = timeoutMs;
    }

    int retries = 0;
    if (ParseInt32(ReadPayloadValue(payload, {"max_retries", "maxRetries"}), &retries) && retries >= 0) {
        streamIntent.constraints.maxRetries = retries;
    }

    int delayMs = 0;
    if (ParseInt32(ReadPayloadValue(payload, {"delay_ms", "delayMs"}), &delayMs) && delayMs >= 0) {
        streamIntent.params.values["delay_ms"] = std::to_wstring(delayMs);
    }

    int holdMs = 0;
    if (ParseInt32(ReadPayloadValue(payload, {"hold_ms", "holdMs"}), &holdMs) && holdMs >= 0) {
        streamIntent.params.values["hold_ms"] = std::to_wstring(holdMs);
    }

    int sequenceMs = 0;
    if (ParseInt32(ReadPayloadValue(payload, {"sequence_ms", "sequenceMs"}), &sequenceMs) && sequenceMs >= 0) {
        streamIntent.params.values["sequence_ms"] = std::to_wstring(sequenceMs);
    }

    *intent = std::move(streamIntent);
    return true;
}

std::size_t ReadRepeatCount(const std::map<std::string, std::string>& payload) {
    std::uint64_t parsed = 0;
    if (!ParseUint64(ReadPayloadValue(payload, {"repeat", "repeat_count"}), &parsed)) {
        return 1U;
    }

    const std::uint64_t bounded = std::clamp<std::uint64_t>(parsed, 1ULL, 64ULL);
    return static_cast<std::size_t>(bounded);
}

std::unordered_map<std::string, std::string> ParseQueryString(const std::string& query) {
    std::unordered_map<std::string, std::string> result;
    if (query.empty()) {
        return result;
    }

    std::size_t start = 0;
    while (start < query.size()) {
        const std::size_t end = query.find('&', start);
        const std::string token = query.substr(start, end == std::string::npos ? std::string::npos : end - start);

        const std::size_t separator = token.find('=');
        if (separator != std::string::npos && separator > 0) {
            result[token.substr(0, separator)] = token.substr(separator + 1U);
        }

        if (end == std::string::npos) {
            break;
        }

        start = end + 1U;
    }

    return result;
}

bool ParseRequestLine(const std::string& request, std::string* method, std::string* path) {
    if (method == nullptr || path == nullptr) {
        return false;
    }

    const std::size_t firstSpace = request.find(' ');
    const std::size_t secondSpace = firstSpace == std::string::npos ? std::string::npos : request.find(' ', firstSpace + 1U);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        return false;
    }

    *method = request.substr(0, firstSpace);
    *path = request.substr(firstSpace + 1U, secondSpace - firstSpace - 1U);
    return true;
}

std::size_t ReadQuerySize(
    const std::unordered_map<std::string, std::string>& query,
    const std::string& key,
    std::size_t defaultValue,
    std::size_t minValue,
    std::size_t maxValue) {
    const auto it = query.find(key);
    if (it == query.end()) {
        return defaultValue;
    }

    std::uint64_t parsed = 0;
    if (!ParseUint64(it->second, &parsed)) {
        return defaultValue;
    }

    const std::size_t clamped = static_cast<std::size_t>(std::clamp<std::uint64_t>(parsed, minValue, maxValue));
    return clamped;
}

int ReadQueryInt(
    const std::unordered_map<std::string, std::string>& query,
    const std::string& key,
    int defaultValue,
    int minValue,
    int maxValue) {
    const auto it = query.find(key);
    if (it == query.end()) {
        return defaultValue;
    }

    int parsed = 0;
    if (!ParseInt32(it->second, &parsed)) {
        return defaultValue;
    }

    return std::clamp(parsed, minValue, maxValue);
}

bool SendAll(SOCKET socket, const std::string& payload) {
    std::size_t sent = 0;
    while (sent < payload.size()) {
        const int written = send(
            socket,
            payload.data() + static_cast<std::ptrdiff_t>(sent),
            static_cast<int>(payload.size() - sent),
            0);
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

bool ReadQueryBool(
    const std::unordered_map<std::string, std::string>& query,
    const std::string& key,
    bool defaultValue) {
    const auto it = query.find(key);
    if (it == query.end()) {
        return defaultValue;
    }

    const std::string value = ToAsciiLower(it->second);
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }

    return defaultValue;
}

std::string UrlDecode(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());

    auto hexValue = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        return -1;
    };

    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '%' && (index + 2U) < value.size()) {
            const int high = hexValue(value[index + 1U]);
            const int low = hexValue(value[index + 2U]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2U;
                continue;
            }
        }

        if (ch == '+') {
            decoded.push_back(' ');
        } else {
            decoded.push_back(ch);
        }
    }

    return decoded;
}

bool CaptureEnvironmentState(
    const std::unique_ptr<ControlRuntime>& controlRuntime,
    const std::shared_ptr<EnvironmentAdapter>& fallbackAdapter,
    EnvironmentState* state,
    bool* runtimeActiveOut) {
    if (state == nullptr) {
        return false;
    }

    const bool runtimeActive = controlRuntime != nullptr && controlRuntime->Status().active;
    if (runtimeActiveOut != nullptr) {
        *runtimeActiveOut = runtimeActive;
    }

    bool captured = false;
    if (runtimeActive) {
        captured = controlRuntime->LatestEnvironmentState(state);
    }

    if (!captured && fallbackAdapter != nullptr) {
        std::string captureError;
        captured = fallbackAdapter->CaptureState(state, &captureError);
    }

    return captured;
}

void EnsureUnifiedState(EnvironmentState* state) {
    if (state == nullptr) {
        return;
    }

    if (state->perception.regions.empty() && !state->uiElements.empty()) {
        state->perception = LightweightPerception::Analyze(*state);
    }

    if (!state->screenFrame.valid) {
        state->screenFrame.frameId = state->sequence;
        state->screenFrame.capturedAt = state->capturedAt;
        state->screenFrame.width = std::max(1, GetSystemMetrics(SM_CXSCREEN));
        state->screenFrame.height = std::max(1, GetSystemMetrics(SM_CYSCREEN));
        state->screenFrame.simulated = true;
        state->screenFrame.valid = true;
    }

    if (state->screenFrame.frameId == 0) {
        state->screenFrame.frameId = state->sequence;
    }

    if (!state->screenState.valid || state->screenState.frameId == 0) {
        const std::vector<VisualElement> visual = VisualDetector::Detect(state->screenFrame, state->uiElements);
        state->screenState = ScreenStateAssembler::Build(
            state->sequence,
            state->capturedAt,
            state->cursorPosition,
            state->uiElements,
            state->screenFrame,
            visual);
    }

    if (state->screenState.environmentSequence == 0) {
        state->screenState.environmentSequence = state->sequence;
    }

    const bool rebuildGraph = !state->unifiedState.interactionGraph.valid ||
        state->unifiedState.interactionGraph.sequence != state->sequence;
    if (rebuildGraph) {
        state->unifiedState.interactionGraph = InteractionGraphBuilder::Build(state->uiElements, state->sequence);
    }

    state->unifiedState.frameId = state->screenState.frameId;
    state->unifiedState.environmentSequence = state->sequence;
    state->unifiedState.capturedAt = state->capturedAt;
    state->unifiedState.screenState = state->screenState;
    state->unifiedState.signature =
        state->screenState.signature ^ (state->unifiedState.interactionGraph.signature << 1U);
    state->unifiedState.valid = state->screenState.valid &&
        (state->uiElements.empty() || state->unifiedState.interactionGraph.valid);
}

class HeuristicPredictor final : public Predictor {
public:
    std::string Name() const override {
        return "heuristic";
    }

    StateSnapshot Predict(const Intent& intent, const StateSnapshot& current, std::string* diagnostics) override {
        StateSnapshot predicted = current;
        predicted.sequence = current.sequence + 1;
        predicted.sourceSnapshotVersion = current.sourceSnapshotVersion == 0
            ? current.sequence
            : current.sourceSnapshotVersion;
        predicted.capturedAt = std::chrono::system_clock::now();
        predicted.simulated = true;

        if (intent.action == IntentAction::Create) {
            predicted.perception.occupancyRatio = std::clamp(predicted.perception.occupancyRatio + 0.01, 0.0, 1.0);
        } else if (intent.action == IntentAction::Delete) {
            predicted.perception.occupancyRatio = std::clamp(predicted.perception.occupancyRatio - 0.01, 0.0, 1.0);
        }

        if (diagnostics != nullptr) {
            *diagnostics = "heuristic predictor used";
        }

        return predicted;
    }
};

}  // namespace

IntentApiServer::IntentApiServer(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry)
    : registry_(registry),
      executionEngine_(executionEngine),
      telemetry_(telemetry),
      streamEnvironmentAdapter_(std::make_shared<RegistryEnvironmentAdapter>(registry_)),
      predictor_(std::make_shared<HeuristicPredictor>()),
      startedAt_(std::chrono::steady_clock::now()) {}

void IntentApiServer::SetPredictor(std::shared_ptr<Predictor> predictor) {
    std::shared_ptr<Predictor> activePredictor;
    {
        std::lock_guard<std::mutex> lock(predictorMutex_);
        if (predictor == nullptr) {
            predictor_ = std::make_shared<HeuristicPredictor>();
        } else {
            predictor_ = std::move(predictor);
        }
        activePredictor = predictor_;
    }

    if (controlRuntime_) {
        controlRuntime_->SetPredictor(activePredictor);
    }
}

int IntentApiServer::Run(std::uint16_t port, bool singleRequest, std::size_t maxRequests) {
    WSADATA wsaData{};
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    const BOOL reuseAddress = TRUE;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(server);
        WSACleanup();
        return 1;
    }

    if (listen(server, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(server);
        WSACleanup();
        return 1;
    }

    std::size_t servedRequests = 0;
    bool running = true;
    std::atomic<int> activeClients{0};
    constexpr int kMaxConcurrentClients = 16;

    const auto processClient = [this](SOCKET client) {
        const int timeoutMs = 3000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

        std::string request;
        request.reserve(16384);

        std::array<char, 4096> buffer{};
        std::size_t expectedBodyLength = 0;
        std::size_t bodyStart = std::string::npos;
        bool requestError = false;
        std::string errorResponse;

        while (true) {
            const int bytes = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (bytes == 0) {
                break;
            }

            if (bytes < 0) {
                const int socketError = WSAGetLastError();
                if (socketError == WSAETIMEDOUT) {
                    errorResponse = BuildErrorResponse(408, "Request Timeout", "request_timeout", "Request body timeout");
                } else {
                    errorResponse = BuildErrorResponse(400, "Bad Request", "socket_error", "Failed to read request");
                }
                requestError = true;
                break;
            }

            request.append(buffer.data(), static_cast<std::size_t>(bytes));
            if (request.size() > (1U << 20U)) {
                errorResponse = BuildErrorResponse(413, "Payload Too Large", "payload_too_large", "Request exceeds 1MB limit");
                requestError = true;
                break;
            }

            if (bodyStart == std::string::npos) {
                bodyStart = request.find("\r\n\r\n");
                if (bodyStart != std::string::npos) {
                    expectedBodyLength = ParseContentLength(std::string_view(request.data(), bodyStart));
                    bodyStart += 4U;
                    if (expectedBodyLength > (1U << 20U)) {
                        errorResponse = BuildErrorResponse(413, "Payload Too Large", "payload_too_large", "Body exceeds 1MB limit");
                        requestError = true;
                        break;
                    }
                    if (expectedBodyLength == 0) {
                        break;
                    }
                }
            }

            if (bodyStart != std::string::npos) {
                const std::size_t currentBodyLength = request.size() >= bodyStart ? request.size() - bodyStart : 0;
                if (currentBodyLength >= expectedBodyLength) {
                    break;
                }
            }
        }

        std::string response;
        if (requestError) {
            response = std::move(errorResponse);
            SendAll(client, response);
            closesocket(client);
            return;
        }

        if (request.empty()) {
            response = BuildErrorResponse(400, "Bad Request", "empty_request", "Request is empty");
            SendAll(client, response);
            closesocket(client);
            return;
        }

        std::string method;
        std::string path;
        const bool parsedLine = ParseRequestLine(request, &method, &path);
        if (parsedLine && method == "GET" && path.rfind("/stream/live", 0) == 0) {
            const std::size_t queryPos = path.find('?');
            const std::unordered_map<std::string, std::string> query = ParseQueryString(
                queryPos == std::string::npos ? std::string() : path.substr(queryPos + 1U));

            const std::size_t eventLimit = ReadQuerySize(query, "events", 20U, 1U, 200U);
            const int intervalMs = ReadQueryInt(query, "interval_ms", 200, 25, 2000);

            const std::string headers =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: close\r\n"
                "X-Accel-Buffering: no\r\n\r\n";

            if (!SendAll(client, headers)) {
                closesocket(client);
                return;
            }

            for (std::size_t eventIndex = 0; eventIndex < eventLimit; ++eventIndex) {
                const TelemetrySnapshot telemetrySnapshot = telemetry_.Snapshot();
                const bool runtimeActive = controlRuntime_ != nullptr && controlRuntime_->Status().active;
                const ControlRuntimeSnapshot runtimeStatus = runtimeActive
                    ? controlRuntime_->Status()
                    : ControlRuntimeSnapshot{};

                std::ostringstream payload;
                payload << "{";
                payload << "\"event\":" << eventIndex << ",";
                payload << "\"timestamp_ms\":" << EpochMs(std::chrono::system_clock::now()) << ",";
                payload << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
                payload << "\"total_executions\":" << telemetrySnapshot.totalExecutions << ",";
                payload << "\"success_rate\":" << telemetrySnapshot.successRate << ",";
                payload << "\"average_latency_ms\":" << telemetrySnapshot.averageLatencyMs;
                if (runtimeActive) {
                    payload << ",\"control\":" << ControlRuntime::SerializeSnapshotJson(runtimeStatus);
                }
                payload << "}";

                const std::string event = "event: runtime\\n" +
                    std::string("data: ") + payload.str() + "\\n\\n";
                if (!SendAll(client, event)) {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
            }

            closesocket(client);
            return;
        }

        response = HandleRequest(request);
        SendAll(client, response);
        closesocket(client);
    };

    while (running) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            break;
        }

        if (!singleRequest && maxRequests == 0) {
            if (activeClients.load() >= kMaxConcurrentClients) {
                const std::string busyResponse =
                    BuildErrorResponse(503, "Service Unavailable", "server_busy", "Too many concurrent requests");
                send(client, busyResponse.c_str(), static_cast<int>(busyResponse.size()), 0);
                closesocket(client);
                continue;
            }

            ++activeClients;
            std::thread([processClient, client, &activeClients]() {
                processClient(client);
                --activeClients;
            }).detach();
            continue;
        }

        processClient(client);
        ++servedRequests;

        if (singleRequest || (maxRequests > 0 && servedRequests >= maxRequests)) {
            running = false;
        }
    }

    closesocket(server);
    WSACleanup();
    return 0;
}

std::string IntentApiServer::HandleRequestForTesting(const std::string& request) {
    return HandleRequest(request);
}

ControlRuntime& IntentApiServer::EnsureControlRuntime() {
    if (!controlRuntime_) {
        std::shared_ptr<Predictor> activePredictor;
        {
            std::lock_guard<std::mutex> lock(predictorMutex_);
            activePredictor = predictor_;
        }

        controlRuntime_ = std::make_unique<ControlRuntime>(
            registry_,
            executionEngine_,
            executionEngine_.Events(),
            telemetry_,
            streamEnvironmentAdapter_);
        controlRuntime_->SetPredictor(activePredictor);
    }

    return *controlRuntime_;
}

std::string IntentApiServer::HandleRequest(const std::string& request) {
    std::string method;
    std::string pathWithQuery;
    if (!ParseRequestLine(request, &method, &pathWithQuery)) {
        return BuildErrorResponse(400, "Bad Request", "malformed_request_line", "Malformed request line");
    }

    std::string path = pathWithQuery;
    std::string queryString;
    const std::size_t queryPos = path.find('?');
    if (queryPos != std::string::npos) {
        queryString = path.substr(queryPos + 1U);
        path = path.substr(0, queryPos);
    }
    const std::unordered_map<std::string, std::string> query = ParseQueryString(queryString);

    const std::size_t bodyPos = request.find("\r\n\r\n");
    const std::string body = bodyPos == std::string::npos ? std::string() : request.substr(bodyPos + 4U);

    if (method == "GET" && path == "/health") {
        const TelemetrySnapshot snapshot = telemetry_.Snapshot();
        const auto serverUptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt_)
                                         .count();
        const bool controlActive = controlRuntime_ != nullptr && controlRuntime_->Status().active;

        std::ostringstream json;
        json << "{";
        json << "\"status\":\"ok\",";
        json << "\"uptime_ms\":" << snapshot.uptimeMs << ",";
        json << "\"server_uptime_ms\":" << serverUptimeMs << ",";
        json << "\"total_executions\":" << snapshot.totalExecutions << ",";
        json << "\"success_rate\":" << snapshot.successRate << ",";
        json << "\"average_latency_ms\":" << snapshot.averageLatencyMs << ",";
        json << "\"control_active\":" << (controlActive ? "true" : "false") << ",";
        json << "\"persisted_traces\":" << snapshot.persistedTraceCount;
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/intents") {
        registry_.Refresh();
        const auto intents = registry_.ListIntents();

        std::ostringstream json;
        json << "{\"count\":" << intents.size() << ",\"intents\":[";
        for (std::size_t index = 0; index < intents.size(); ++index) {
            if (index > 0) {
                json << ',';
            }
            json << intents[index].Serialize();
        }
        json << "]}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/capabilities") {
        registry_.Refresh();
        const auto intents = registry_.ListIntents();

        std::map<std::string, std::set<std::string>> sourcesByAction;
        std::map<std::string, std::size_t> countByAction;

        for (const auto& intent : intents) {
            const std::string action = ToString(intent.action);
            ++countByAction[action];
            sourcesByAction[action].insert(intent.source);
        }

        std::ostringstream json;
        json << "{\"capabilities\":[";
        bool firstAction = true;
        for (const auto& entry : countByAction) {
            if (!firstAction) {
                json << ',';
            }
            firstAction = false;

            json << "{";
            json << "\"action\":\"" << EscapeJson(entry.first) << "\",";
            json << "\"count\":" << entry.second << ",";
            json << "\"sources\":[";

            bool firstSource = true;
            for (const auto& source : sourcesByAction[entry.first]) {
                if (!firstSource) {
                    json << ',';
                }
                firstSource = false;
                json << "\"" << EscapeJson(source) << "\"";
            }

            json << "]";
            json << "}";
        }
        json << "]}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/capabilities/full") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(
                503,
                "Service Unavailable",
                "capabilities_unavailable",
                "Unable to capture environment state for full capabilities");
        }

        EnsureUnifiedState(&state);

        const bool includeHidden = ReadQueryBool(query, "include_hidden", true);
        const std::size_t limit = ReadQuerySize(query, "limit", 4096U, 1U, 32768U);

        const std::vector<Intent> fullCapabilities = InteractionGraphBuilder::GenerateIntents(
            state.unifiedState.interactionGraph,
            includeHidden,
            limit);

        std::map<std::string, std::size_t> actionCounts;
        std::size_t hiddenCount = 0;
        std::size_t offscreenCount = 0;
        std::size_t collapsedCount = 0;

        for (const auto& entry : state.unifiedState.interactionGraph.nodes) {
            const InteractionNode& node = entry.second;
            if (node.hidden) {
                ++hiddenCount;
            }
            if (node.offscreen) {
                ++offscreenCount;
            }
            if (node.collapsed) {
                ++collapsedCount;
            }
        }

        for (const Intent& intent : fullCapabilities) {
            ++actionCounts[ToString(intent.action)];
        }

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"include_hidden\":" << (includeHidden ? "true" : "false") << ",";
        json << "\"graph_version\":" << state.unifiedState.interactionGraph.version << ",";
        json << "\"graph_signature\":" << state.unifiedState.interactionGraph.signature << ",";
        json << "\"node_count\":" << state.unifiedState.interactionGraph.nodes.size() << ",";
        json << "\"edge_count\":" << state.unifiedState.interactionGraph.edges.size() << ",";
        json << "\"command_count\":" << state.unifiedState.interactionGraph.commands.size() << ",";
        json << "\"hidden_node_count\":" << hiddenCount << ",";
        json << "\"offscreen_node_count\":" << offscreenCount << ",";
        json << "\"collapsed_node_count\":" << collapsedCount << ",";
        json << "\"capability_count\":" << fullCapabilities.size() << ",";
        json << "\"actions\":[";

        bool firstAction = true;
        for (const auto& entry : actionCounts) {
            if (!firstAction) {
                json << ",";
            }
            firstAction = false;
            json << "{";
            json << "\"action\":\"" << EscapeJson(entry.first) << "\",";
            json << "\"count\":" << entry.second;
            json << "}";
        }

        json << "],";
        json << "\"capabilities\":[";
        for (std::size_t index = 0; index < fullCapabilities.size(); ++index) {
            if (index > 0) {
                json << ",";
            }
            json << fullCapabilities[index].Serialize();
        }
        json << "]";
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/telemetry/persistence") {
        return BuildResponse(200, "OK", telemetry_.SerializePersistenceJson());
    }

    if (method == "GET" && path == "/control/status") {
        if (!controlRuntime_) {
            ControlRuntimeSnapshot empty;
            return BuildResponse(200, "OK", ControlRuntime::SerializeSnapshotJson(empty));
        }

        return BuildResponse(200, "OK", ControlRuntime::SerializeSnapshotJson(controlRuntime_->Status()));
    }

    if (method == "GET" && path == "/stream/state") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "stream_state_unavailable", "Unable to capture environment state");
        }

        EnsureUnifiedState(&state);

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"state\":" << SerializeEnvironmentStateJson(state) << ",";
        json << "\"latency\":" << telemetry_.SerializeLatencyJson(200);
        if (runtimeActive) {
            json << ",\"control_status\":" << ControlRuntime::SerializeSnapshotJson(controlRuntime_->Status());
        }
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/stream/frame") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "stream_frame_unavailable", "Unable to capture screen state");
        }

        EnsureUnifiedState(&state);

        VisionLatencySample visionSample;
        visionSample.frameId = state.screenState.frameId;
        visionSample.environmentSequence = state.screenState.environmentSequence;
        visionSample.captureMs = static_cast<double>(state.visionTiming.captureMs);
        visionSample.detectionMs = static_cast<double>(state.visionTiming.detectionMs);
        visionSample.mergeMs = static_cast<double>(state.visionTiming.mergeMs);
        visionSample.totalMs = static_cast<double>(state.visionTiming.totalMs);
        visionSample.simulated = state.screenState.simulated;
        visionSample.timestamp = state.screenState.capturedAt;
        telemetry_.LogVisionSample(visionSample);

        const auto modeIt = query.find("mode");
        const std::string mode = modeIt == query.end() ? "full" : ToAsciiLower(modeIt->second);
        const bool deltaMode = mode == "delta";

        std::uint64_t sinceFrameId = 0;
        const auto sinceIt = query.find("since");
        if (sinceIt != query.end()) {
            ParseUint64(sinceIt->second, &sinceFrameId);
        }

        ScreenState baseFrame;
        bool hasBaseFrame = false;

        {
            std::lock_guard<std::mutex> lock(frameHistoryMutex_);

            if (sinceFrameId > 0) {
                for (auto it = frameHistory_.rbegin(); it != frameHistory_.rend(); ++it) {
                    if (it->frameId == sinceFrameId) {
                        baseFrame = *it;
                        hasBaseFrame = true;
                        break;
                    }
                }
            } else if (deltaMode && !frameHistory_.empty()) {
                baseFrame = frameHistory_.back();
                hasBaseFrame = true;
            }

            if (frameHistory_.empty() || frameHistory_.back().frameId != state.screenState.frameId) {
                frameHistory_.push_back(state.screenState);
                while (frameHistory_.size() > 128U) {
                    frameHistory_.pop_front();
                }
            }
        }

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"frame_id\":" << state.screenState.frameId << ",";
        json << "\"signature\":" << state.screenState.signature << ",";
        json << "\"mode\":\"" << (deltaMode ? "delta" : "full") << "\",";

        if (!deltaMode) {
            json << "\"state\":" << SerializeScreenStateJson(state.screenState) << ",";
        } else {
            json << "\"since\":" << sinceFrameId << ",";

            if (sinceFrameId == state.screenState.frameId && sinceFrameId != 0) {
                json << "\"delta\":{\"changed\":false,\"added\":[],\"updated\":[],\"removed\":[]},";
            } else if (hasBaseFrame) {
                bool changed = false;
                const std::string deltaJson = SerializeScreenDeltaJson(baseFrame, state.screenState, &changed);
                json << "\"delta\":" << deltaJson << ",";
                json << "\"reset_required\":false,";
            } else if (sinceFrameId > 0) {
                json << "\"delta\":{\"changed\":true,\"added\":[],\"updated\":[],\"removed\":[]},";
                json << "\"reset_required\":true,";
                json << "\"state\":" << SerializeScreenStateJson(state.screenState) << ",";
            } else {
                const ScreenState emptyBase;
                bool changed = false;
                const std::string deltaJson = SerializeScreenDeltaJson(emptyBase, state.screenState, &changed);
                json << "\"delta\":" << deltaJson << ",";
                json << "\"reset_required\":false,";
            }
        }

        json << "\"vision\":" << telemetry_.SerializeVisionJson(200);
        if (runtimeActive) {
            json << ",\"control_status\":" << ControlRuntime::SerializeSnapshotJson(controlRuntime_->Status());
        }
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/interaction-graph") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(
                503,
                "Service Unavailable",
                "interaction_graph_unavailable",
                "Unable to capture interaction graph state");
        }

        EnsureUnifiedState(&state);

        const InteractionGraph& currentGraph = state.unifiedState.interactionGraph;
        const auto deltaSinceIt = query.find("delta_since");
        const bool deltaRequested = deltaSinceIt != query.end();

        std::uint64_t deltaSinceVersion = 0;
        if (deltaRequested) {
            ParseUint64(deltaSinceIt->second, &deltaSinceVersion);
        }

        GraphDelta delta;
        bool includeDelta = false;

        {
            std::lock_guard<std::mutex> lock(graphHistoryMutex_);

            if (graphHistory_.empty() ||
                graphHistory_.back().version != currentGraph.version ||
                graphHistory_.back().signature != currentGraph.signature) {
                graphHistory_.push_back(currentGraph);
                while (graphHistory_.size() > 128U) {
                    graphHistory_.pop_front();
                }
            }

            if (deltaRequested) {
                includeDelta = true;

                if (deltaSinceVersion == 0) {
                    InteractionGraph emptyGraph;
                    delta = InteractionGraphBuilder::ComputeDelta(emptyGraph, currentGraph);
                    delta.fromVersion = 0;
                    delta.toVersion = currentGraph.version;
                } else if (deltaSinceVersion == currentGraph.version) {
                    delta.fromVersion = deltaSinceVersion;
                    delta.toVersion = currentGraph.version;
                    delta.changed = false;
                } else {
                    bool foundBase = false;
                    for (const InteractionGraph& historical : graphHistory_) {
                        if (historical.version == deltaSinceVersion) {
                            delta = InteractionGraphBuilder::ComputeDelta(historical, currentGraph);
                            foundBase = true;
                            break;
                        }
                    }

                    if (!foundBase) {
                        delta.fromVersion = deltaSinceVersion;
                        delta.toVersion = currentGraph.version;
                        delta.changed = true;
                        delta.resetRequired = true;
                    }
                }
            }
        }

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"frame_id\":" << state.unifiedState.frameId << ",";
        json << "\"sequence\":" << state.sequence << ",";
        json << "\"signature\":" << state.unifiedState.signature << ",";
        json << "\"version\":" << currentGraph.version << ",";
        json << "\"graph\":" << InteractionGraphBuilder::SerializeGraphJson(currentGraph);
        if (includeDelta) {
            json << ",\"delta_since\":" << deltaSinceVersion << ",";
            json << "\"delta\":" << InteractionGraphBuilder::SerializeDeltaJson(delta);
        }
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path.rfind("/interaction-node/", 0) == 0) {
        const std::string rawNodeId = path.substr(std::string("/interaction-node/").size());
        const std::string nodeId = UrlDecode(rawNodeId);
        if (nodeId.empty()) {
            return BuildErrorResponse(400, "Bad Request", "missing_node_id", "Missing interaction node id");
        }

        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(
                503,
                "Service Unavailable",
                "interaction_node_unavailable",
                "Unable to capture interaction graph state");
        }

        EnsureUnifiedState(&state);

        const auto node = InteractionGraphBuilder::FindNode(state.unifiedState.interactionGraph, nodeId);
        if (!node.has_value()) {
            return BuildErrorResponse(404, "Not Found", "node_not_found", "Interaction node id was not found");
        }

        const Intent mappedIntent = InteractionGraphBuilder::GenerateIntent(*node);

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"sequence\":" << state.sequence << ",";
        json << "\"node\":" << InteractionGraphBuilder::SerializeNodeJson(*node) << ",";
        json << "\"intent\":" << mappedIntent.Serialize() << ",";
        json << "\"execution_plan\":" << InteractionGraphBuilder::SerializeExecutionPlanJson(node->executionPlan) << ",";
        json << "\"reveal_strategy\":" << InteractionGraphBuilder::SerializeRevealStrategyJson(node->revealStrategy) << ",";
        json << "\"intent_binding\":" << InteractionGraphBuilder::SerializeIntentBindingJson(node->intentBinding);
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/stream/live") {
        const std::size_t events = ReadQuerySize(query, "events", 20U, 1U, 200U);
        const int intervalMs = ReadQueryInt(query, "interval_ms", 200, 25, 2000);

        std::ostringstream json;
        json << "{";
        json << "\"stream\":\"live\",";
        json << "\"transport\":\"sse\",";
        json << "\"events\":" << events << ",";
        json << "\"interval_ms\":" << intervalMs;
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/perf") {
        double targetBudgetMs = 16.0;
        if (controlRuntime_ != nullptr && controlRuntime_->Status().active) {
            targetBudgetMs = static_cast<double>(std::max<std::int64_t>(1LL, controlRuntime_->Status().targetFrameMs));
        }

        const auto targetIt = query.find("target_ms");
        if (targetIt != query.end()) {
            int parsedTarget = 0;
            if (ParseInt32(targetIt->second, &parsedTarget) && parsedTarget > 0) {
                targetBudgetMs = static_cast<double>(parsedTarget);
            }
        }

        const std::size_t limit = ReadQuerySize(query, "limit", 200U, 1U, 4096U);
        const bool runtimeActive = controlRuntime_ != nullptr && controlRuntime_->Status().active;

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"contract\":" << telemetry_.SerializePerformanceContractJson(targetBudgetMs, limit);
        if (runtimeActive) {
            json << ",\"control_status\":" << ControlRuntime::SerializeSnapshotJson(controlRuntime_->Status());
        }
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "POST" && path == "/control/start") {
        std::map<std::string, std::string> payload;
        if (!body.empty()) {
            std::string jsonError;
            if (!ParseFlatJsonObject(body, &payload, &jsonError)) {
                return BuildErrorResponse(400, "Bad Request", "invalid_json", jsonError);
            }
        }

        ControlRuntimeConfig config;
        int targetFrameMs = 0;
        if (ParseInt32(
                ReadPayloadValue(payload, {"latencyBudgetMs", "latency_budget_ms", "targetFrameMs", "target_frame_ms"}),
                &targetFrameMs)) {
            config.targetFrameMs = targetFrameMs;
        }

        std::uint64_t maxFrames = 0;
        if (ParseUint64(ReadPayloadValue(payload, {"maxFrames", "max_frames"}), &maxFrames)) {
            if (maxFrames > 1000000ULL) {
                return BuildErrorResponse(400, "Bad Request", "max_frames_exceeded", "maxFrames exceeds safety limit");
            }
            config.maxFrames = maxFrames;
        }

        int observationIntervalMs = 0;
        if (ParseInt32(
                ReadPayloadValue(payload, {"observationIntervalMs", "observation_interval_ms"}),
                &observationIntervalMs)) {
            config.observationIntervalMs = observationIntervalMs;
        }

        int decisionBudgetMs = 0;
        if (ParseInt32(
                ReadPayloadValue(payload, {"decisionBudgetMs", "decision_budget_ms"}),
                &decisionBudgetMs)) {
            config.decisionBudgetMs = decisionBudgetMs;
        }

        ControlRuntime& runtime = EnsureControlRuntime();
        std::string message;
        const bool started = runtime.Start(config, &message);
        const ControlRuntimeSnapshot status = runtime.Status();

        std::ostringstream json;
        json << "{";
        json << "\"started\":" << (started ? "true" : "false") << ",";
        json << "\"message\":\"" << EscapeJson(message) << "\",";
        json << "\"status\":" << ControlRuntime::SerializeSnapshotJson(status);
        json << "}";

        return BuildResponse(started ? 200 : 409, started ? "OK" : "Conflict", json.str());
    }

    if (method == "POST" && path == "/control/stop") {
        if (!controlRuntime_) {
            return BuildErrorResponse(409, "Conflict", "control_not_running", "Control runtime is not running");
        }

        const ControlRuntimeSummary summary = controlRuntime_->Stop();
        std::ostringstream json;
        json << "{";
        json << "\"summary\":" << ControlRuntime::SerializeSummaryJson(summary) << ",";
        json << "\"persistence\":" << telemetry_.SerializePersistenceJson();
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "POST" && path == "/stream/control") {
        std::map<std::string, std::string> payload;
        std::string jsonError;
        if (!ParseFlatJsonObject(body, &payload, &jsonError)) {
            return BuildErrorResponse(400, "Bad Request", "invalid_json", jsonError);
        }

        const std::string dsl = ReadPayloadValue(payload, {"sequence", "macro"});

        ActionSequence sequence;
        if (!dsl.empty()) {
            Intent templateIntent;
            templateIntent.source = "stream";
            templateIntent.confidence = 1.0F;

            std::string parseError;
            if (!ParseActionSequenceDsl(dsl, templateIntent, &sequence, &parseError)) {
                return BuildErrorResponse(400, "Bad Request", "invalid_sequence", parseError);
            }
        } else {
            Intent streamIntent;
            std::string errorCode;
            std::string errorMessage;
            if (!BuildStreamIntent(payload, &streamIntent, &errorCode, &errorMessage)) {
                return BuildErrorResponse(400, "Bad Request", errorCode, errorMessage);
            }

            const std::size_t repeat = ReadRepeatCount(payload);
            sequence = BuildRepeatedSequence(streamIntent, repeat, "stream-control");
        }

        const std::string executionMode = ToAsciiLower(ReadPayloadValue(payload, {"mode", "execution_mode"}));
        if (executionMode == "queued" || executionMode == "realtime") {
            if (!controlRuntime_ || !controlRuntime_->Status().active) {
                return BuildErrorResponse(
                    409,
                    "Conflict",
                    "control_not_running",
                    "Control runtime must be running for queued stream control");
            }

            const std::string priorityRaw = ToAsciiLower(ReadPayloadValue(payload, {"priority"}));
            ControlPriority priority = ControlPriority::Medium;
            if (priorityRaw == "high") {
                priority = ControlPriority::High;
            } else if (priorityRaw == "low") {
                priority = ControlPriority::Low;
            }

            std::size_t queuedCount = 0;
            for (const ActionStep& step : sequence.steps) {
                if (!controlRuntime_->EnqueueIntent(step.intent, priority)) {
                    return BuildErrorResponse(409, "Conflict", "control_enqueue_failed", "Unable to enqueue stream step");
                }
                ++queuedCount;
            }

            std::ostringstream queuedJson;
            queuedJson << "{";
            queuedJson << "\"queued\":true,";
            queuedJson << "\"queued_count\":" << queuedCount << ",";
            queuedJson << "\"priority\":\"" << EscapeJson(priorityRaw.empty() ? "medium" : priorityRaw) << "\",";
            queuedJson << "\"status\":" << ControlRuntime::SerializeSnapshotJson(controlRuntime_->Status());
            queuedJson << "}";
            return BuildResponse(202, "Accepted", queuedJson.str());
        }

        EnvironmentState synchronizedState;
        bool hasSynchronizedState = false;
        if (controlRuntime_ != nullptr && controlRuntime_->Status().active) {
            hasSynchronizedState = controlRuntime_->LatestEnvironmentState(&synchronizedState);
        }

        if (!hasSynchronizedState && streamEnvironmentAdapter_ != nullptr) {
            std::string captureError;
            hasSynchronizedState = streamEnvironmentAdapter_->CaptureState(&synchronizedState, &captureError);
        }

        MacroExecutor macroExecutor(executionEngine_);
        const ActionSequenceResult sequenceResult =
            macroExecutor.Execute(sequence, hasSynchronizedState ? &synchronizedState : nullptr);

        std::ostringstream resultJson;
        resultJson << "{";
        resultJson << "\"success\":" << (sequenceResult.success ? "true" : "false") << ",";
        resultJson << "\"sequence_id\":\"" << EscapeJson(sequenceResult.sequenceId) << "\",";
        resultJson << "\"attempted_steps\":" << sequenceResult.attemptedSteps << ",";
        resultJson << "\"completed_steps\":" << sequenceResult.completedSteps << ",";
        resultJson << "\"total_duration_ms\":" << sequenceResult.totalDuration.count() << ",";
        resultJson << "\"message\":\"" << EscapeJson(sequenceResult.message) << "\",";
        resultJson << "\"steps\":[";

        for (std::size_t index = 0; index < sequenceResult.stepResults.size(); ++index) {
            if (index > 0) {
                resultJson << ",";
            }

            const ExecutionResult& stepResult = sequenceResult.stepResults[index];
            resultJson << "{";
            resultJson << "\"index\":" << index << ",";
            resultJson << "\"status\":\"" << EscapeJson(ToString(stepResult.status)) << "\",";
            resultJson << "\"trace_id\":\"" << EscapeJson(stepResult.traceId) << "\",";
            resultJson << "\"method\":\"" << EscapeJson(stepResult.method) << "\",";
            resultJson << "\"message\":\"" << EscapeJson(stepResult.message) << "\",";
            resultJson << "\"duration_ms\":" << stepResult.duration.count() << ",";
            resultJson << "\"verified\":" << (stepResult.verified ? "true" : "false");
            resultJson << "}";
        }

        resultJson << "]";
        if (hasSynchronizedState) {
            resultJson << ",\"state_sequence\":" << synchronizedState.sequence;
        }
        resultJson << "}";

        const int statusCode = sequenceResult.success ? 200 : 500;
        return BuildResponse(statusCode, statusCode == 200 ? "OK" : "Internal Server Error", resultJson.str());
    }

    if (method == "POST" && path == "/predict") {
        std::map<std::string, std::string> payload;
        std::string jsonError;
        if (!ParseFlatJsonObject(body, &payload, &jsonError)) {
            return BuildErrorResponse(400, "Bad Request", "invalid_json", jsonError);
        }

        Intent intent;
        std::string errorCode;
        std::string errorMessage;
        if (!BuildStreamIntent(payload, &intent, &errorCode, &errorMessage)) {
            return BuildErrorResponse(400, "Bad Request", errorCode, errorMessage);
        }

        StateSnapshot current;
        bool captured = false;
        if (controlRuntime_ != nullptr && controlRuntime_->Status().active) {
            captured = controlRuntime_->LatestEnvironmentState(&current);
        }

        if (!captured && streamEnvironmentAdapter_ != nullptr) {
            std::string captureError;
            captured = streamEnvironmentAdapter_->CaptureState(&current, &captureError);
        }

        if (!captured) {
            return BuildErrorResponse(503, "Service Unavailable", "prediction_state_unavailable", "Unable to capture state for prediction");
        }

        std::shared_ptr<Predictor> activePredictor;
        {
            std::lock_guard<std::mutex> lock(predictorMutex_);
            activePredictor = predictor_;
        }

        StateSnapshot predicted;
        std::string diagnostics;
        if (activePredictor != nullptr) {
            predicted = activePredictor->Predict(intent, current, &diagnostics);
        } else {
            predicted = current;
            predicted.sequence = current.sequence + 1;
            predicted.capturedAt = std::chrono::system_clock::now();
            predicted.simulated = true;
            diagnostics = "predictor not configured; passthrough used";
        }

        const FeedbackDelta delta = ComputeFeedbackDelta(current, predicted);

        std::ostringstream json;
        json << "{";
        json << "\"predictor\":\"" << EscapeJson(activePredictor != nullptr ? activePredictor->Name() : "none") << "\",";
        json << "\"diagnostics\":\"" << EscapeJson(diagnostics) << "\",";
        json << "\"intent\":{\"action\":\"" << EscapeJson(ToString(intent.action)) << "\"},";
        json << "\"before\":" << SerializeEnvironmentStateJson(current) << ",";
        json << "\"after\":" << SerializeEnvironmentStateJson(predicted) << ",";
        json << "\"delta\":{";
        json << "\"signature_changed\":" << (delta.signatureChanged ? "true" : "false") << ",";
        json << "\"focus_ratio_delta\":" << delta.focusRatioDelta << ",";
        json << "\"occupancy_ratio_delta\":" << delta.occupancyRatioDelta << ",";
        json << "\"capture_skew_ms\":" << delta.captureSkewMs;
        json << "}";
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "POST" && (path == "/execute" || path == "/explain")) {
        std::map<std::string, std::string> payload;
        std::string jsonError;
        if (!ParseFlatJsonObject(body, &payload, &jsonError)) {
            return BuildErrorResponse(400, "Bad Request", "invalid_json", jsonError);
        }

        const auto actionIt = payload.find("action");
        if (actionIt == payload.end()) {
            return BuildErrorResponse(400, "Bad Request", "missing_action", "Missing required field: action");
        }

        const IntentAction action = IntentActionFromString(actionIt->second);
        if (action == IntentAction::Unknown) {
            return BuildErrorResponse(400, "Bad Request", "unknown_action", "Unknown action");
        }

        registry_.Refresh();

        if (path == "/explain") {
            const auto targetIt = payload.find("target");
            if (targetIt == payload.end() || targetIt->second.empty()) {
                return BuildErrorResponse(400, "Bad Request", "missing_target", "Missing required field: target");
            }

            const std::wstring requestedTarget = Wide(targetIt->second);
            const ResolutionResult resolution = registry_.Resolve(action, requestedTarget);

            std::ostringstream json;
            json << "{";
            json << "\"action\":\"" << EscapeJson(ToString(action)) << "\",";
            json << "\"target\":\"" << EscapeJson(targetIt->second) << "\",";
            json << "\"ambiguous\":" << (resolution.ambiguity.has_value() ? "true" : "false") << ",";
            json << "\"candidates\":[";

            const std::size_t limit = std::min<std::size_t>(resolution.ranked.size(), 8U);
            for (std::size_t index = 0; index < limit; ++index) {
                if (index > 0) {
                    json << ',';
                }
                json << "{";
                json << "\"score\":" << resolution.ranked[index].score << ",";
                json << "\"intent\":" << resolution.ranked[index].intent.Serialize();
                json << "}";
            }
            json << "]";

            if (resolution.bestMatch.has_value()) {
                json << ",\"best\":" << resolution.bestMatch->intent.Serialize();
            }

            json << "}";
            return BuildResponse(200, "OK", json.str());
        }

        Intent intent;
        intent.action = action;
        intent.name = ToString(action);
        intent.source = "api";
        intent.confidence = 1.0F;

        if (IsUiAction(action)) {
            const auto targetIt = payload.find("target");
            if (targetIt == payload.end() || targetIt->second.empty()) {
                return BuildErrorResponse(400, "Bad Request", "missing_target", "Missing required field: target");
            }

            const std::wstring requestedTarget = Wide(targetIt->second);
            const ResolutionResult resolution = registry_.Resolve(action, requestedTarget);
            if (resolution.ambiguity.has_value()) {
                std::ostringstream ambiguity;
                ambiguity << "{\"error\":{";
                ambiguity << "\"code\":\"ambiguous_target\",";
                ambiguity << "\"message\":\"" << EscapeJson(resolution.ambiguity->message) << "\",";
                ambiguity << "\"candidates\":[";
                for (std::size_t i = 0; i < resolution.ambiguity->candidates.size(); ++i) {
                    if (i > 0) {
                        ambiguity << ',';
                    }
                    ambiguity << resolution.ambiguity->candidates[i].intent.Serialize();
                }
                ambiguity << "]}}";
                return BuildResponse(409, "Conflict", ambiguity.str());
            }

            if (resolution.bestMatch.has_value()) {
                intent = resolution.bestMatch->intent;
            }

            intent.action = action;
            intent.name = ToString(action);
            intent.target.type = TargetType::UiElement;
            intent.target.label = requestedTarget;

            if (action == IntentAction::SetValue) {
                const auto valueIt = payload.find("value");
                if (valueIt == payload.end()) {
                    return BuildErrorResponse(400, "Bad Request", "missing_value", "Missing required field: value");
                }
                intent.params.values["value"] = Wide(valueIt->second);
            }
        } else if (IsFileAction(action)) {
            const auto pathIt = payload.find("path");
            if (pathIt == payload.end() || pathIt->second.empty()) {
                return BuildErrorResponse(400, "Bad Request", "missing_path", "Missing required field: path");
            }

            intent.target.type = TargetType::FileSystemPath;
            intent.params.values["path"] = Wide(pathIt->second);
            intent.target.path = intent.params.values["path"];
            intent.target.label = intent.params.values["path"];

            if (action == IntentAction::Move) {
                const auto destinationIt = payload.find("destination");
                if (destinationIt == payload.end() || destinationIt->second.empty()) {
                    return BuildErrorResponse(400, "Bad Request", "missing_destination", "Missing required field: destination");
                }
                intent.params.values["destination"] = Wide(destinationIt->second);
            }
        } else {
            return BuildErrorResponse(400, "Bad Request", "unsupported_action", "Action is not executable");
        }

        const std::string executionMode = ToAsciiLower(ReadPayloadValue(payload, {"mode", "execution_mode"}));
        if (executionMode == "queued" || executionMode == "realtime") {
            if (!controlRuntime_ || !controlRuntime_->Status().active) {
                return BuildErrorResponse(409, "Conflict", "control_not_running", "Control runtime must be running for queued execution");
            }

            const std::string priorityRaw = ToAsciiLower(ReadPayloadValue(payload, {"priority"}));
            ControlPriority priority = ControlPriority::Medium;
            if (priorityRaw == "high") {
                priority = ControlPriority::High;
            } else if (priorityRaw == "low") {
                priority = ControlPriority::Low;
            }

            if (!controlRuntime_->EnqueueIntent(intent, priority)) {
                return BuildErrorResponse(409, "Conflict", "control_enqueue_failed", "Unable to enqueue intent");
            }

            std::ostringstream queuedJson;
            queuedJson << "{";
            queuedJson << "\"queued\":true,";
            queuedJson << "\"priority\":\"" << EscapeJson(priorityRaw.empty() ? "medium" : priorityRaw) << "\",";
            queuedJson << "\"status\":" << ControlRuntime::SerializeSnapshotJson(controlRuntime_->Status());
            queuedJson << "}";
            return BuildResponse(202, "Accepted", queuedJson.str());
        }

        const ExecutionResult result = executionEngine_.Execute(intent);
        if ((result.status == ExecutionStatus::SUCCESS || result.status == ExecutionStatus::PARTIAL) && !intent.id.empty()) {
            registry_.RecordInteraction(intent.id);
        }

        std::ostringstream json;
        json << "{";
        json << "\"trace_id\":\"" << EscapeJson(result.traceId) << "\",";
        json << "\"status\":\"" << EscapeJson(ToString(result.status)) << "\",";
        json << "\"verified\":" << (result.verified ? "true" : "false") << ",";
        json << "\"method\":\"" << EscapeJson(result.method) << "\",";
        json << "\"message\":\"" << EscapeJson(result.message) << "\",";
        json << "\"durationMs\":" << result.duration.count();
        json << "}";

        const int statusCode = result.status == ExecutionStatus::FAILED ? 500 : 200;
        return BuildResponse(statusCode, statusCode == 200 ? "OK" : "Internal Server Error", json.str());
    }

    return BuildErrorResponse(404, "Not Found", "route_not_found", "Route not found");
}

}  // namespace iee