#include <WinSock2.h>
#include <WS2tcpip.h>

#include "IntentApiServer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ActionSequence.h"
#include "ActionInterface.h"
#include "AIStateView.h"
#include "ExecutionContract.h"
#include "TaskInterface.h"

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

bool ParseBoolString(const std::string& value, bool* output) {
    if (output == nullptr) {
        return false;
    }

    const std::string normalized = ToAsciiLower(value);
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        *output = true;
        return true;
    }

    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        *output = false;
        return true;
    }

    return false;
}

std::size_t ParseSizeWithBounds(const std::string& value, std::size_t defaultValue, std::size_t minValue, std::size_t maxValue) {
    std::uint64_t parsed = 0;
    if (!ParseUint64(value, &parsed)) {
        return defaultValue;
    }

    return static_cast<std::size_t>(std::clamp<std::uint64_t>(parsed, minValue, maxValue));
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
    json << "\"lightweight_text_detections\":" << state.perception.lightweightTextDetections << ",";
    json << "\"grouped_region_count\":" << state.perception.groupedRegionCount << ",";
    json << "\"region_labels\":[";

    for (std::size_t index = 0; index < state.perception.regionLabels.size(); ++index) {
        if (index > 0) {
            json << ",";
        }
        json << "\"" << EscapeJson(state.perception.regionLabels[index]) << "\"";
    }

    json << "],";
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

enum class AiFilterMode {
    Interactive,
    Visible,
    Relevant
};

struct AiFilteredNode {
    std::string nodeId;
    std::string label;
    std::string action;
    bool hidden{false};
    bool offscreen{false};
    bool collapsed{false};
    bool requiresReveal{false};
    double score{0.0};
};

AiFilterMode ParseAiFilterMode(const std::unordered_map<std::string, std::string>& query) {
    std::string raw;

    const auto filterIt = query.find("filter");
    if (filterIt != query.end()) {
        raw = ToAsciiLower(filterIt->second);
    }

    if (raw.empty()) {
        const auto modeIt = query.find("mode");
        if (modeIt != query.end()) {
            raw = ToAsciiLower(modeIt->second);
        }
    }

    if (raw == "visible") {
        return AiFilterMode::Visible;
    }
    if (raw == "relevant" || raw == "goal") {
        return AiFilterMode::Relevant;
    }
    return AiFilterMode::Interactive;
}

const char* ToString(AiFilterMode mode) {
    switch (mode) {
    case AiFilterMode::Visible:
        return "visible";
    case AiFilterMode::Relevant:
        return "relevant";
    case AiFilterMode::Interactive:
    default:
        return "interactive";
    }
}

std::vector<std::string> TokenizeRelevance(std::string_view value) {
    std::vector<std::string> tokens;
    std::string current;

    for (const char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
            current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            continue;
        }

        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

double Coverage(const std::vector<std::string>& haystack, const std::vector<std::string>& needles) {
    if (haystack.empty() || needles.empty()) {
        return 0.0;
    }

    std::size_t matches = 0;
    for (const auto& needle : needles) {
        if (std::find(haystack.begin(), haystack.end(), needle) != haystack.end()) {
            ++matches;
        }
    }

    return static_cast<double>(matches) / static_cast<double>(needles.size());
}

double DomainAffinity(TaskDomain domain, const std::vector<std::string>& tokens) {
    static constexpr std::array<const char*, 6> kPresentationKeywords{
        "present", "presentation", "slide", "slideshow", "deck", "export"};
    static constexpr std::array<const char*, 7> kBrowserKeywords{
        "browser", "tab", "address", "search", "url", "navigate", "open"};

    if (domain == TaskDomain::Presentation) {
        std::size_t matches = 0;
        for (const char* keyword : kPresentationKeywords) {
            if (std::find(tokens.begin(), tokens.end(), keyword) != tokens.end()) {
                ++matches;
            }
        }
        return static_cast<double>(matches) / static_cast<double>(kPresentationKeywords.size());
    }

    if (domain == TaskDomain::Browser) {
        std::size_t matches = 0;
        for (const char* keyword : kBrowserKeywords) {
            if (std::find(tokens.begin(), tokens.end(), keyword) != tokens.end()) {
                ++matches;
            }
        }
        return static_cast<double>(matches) / static_cast<double>(kBrowserKeywords.size());
    }

    return 0.0;
}

bool IsInteractiveNode(const InteractionNode& node) {
    return node.executionPlan.executable && node.intentBinding.action != IntentAction::Unknown;
}

bool IsVisibleNode(const InteractionNode& node) {
    return !node.hidden && !node.offscreen && !node.collapsed && node.visible;
}

double ComputeRelevantScore(
    const InteractionNode& node,
    const std::vector<std::string>& goalTokens,
    TaskDomain domain) {
    const std::string corpus = ToAsciiLower(node.label + " " + node.shortcut + " " + node.type + " " + ToString(node.intentBinding.action));
    const std::vector<std::string> nodeTokens = TokenizeRelevance(corpus);

    double score = 0.0;
    score += 0.70 * Coverage(nodeTokens, goalTokens);
    score += 0.20 * DomainAffinity(domain, nodeTokens);
    score += 0.10 * (node.executionPlan.executable ? 1.0 : 0.0);

    if (node.hidden || node.offscreen || node.collapsed) {
        score -= 0.08;
    }

    return std::clamp(score, 0.0, 1.0);
}

std::vector<AiFilteredNode> BuildAiFilteredNodes(
    const InteractionGraph& graph,
    AiFilterMode mode,
    std::string goal,
    TaskDomain domain,
    bool includeHidden,
    std::size_t topN) {
    std::vector<AiFilteredNode> filtered;
    filtered.reserve(graph.nodes.size());

    const std::vector<std::string> goalTokens = TokenizeRelevance(ToAsciiLower(goal));

    for (const auto& entry : graph.nodes) {
        const InteractionNode& node = entry.second;

        if (!includeHidden && (node.hidden || node.offscreen || node.collapsed)) {
            continue;
        }

        AiFilteredNode candidate;
        candidate.nodeId = node.id;
        candidate.label = node.label;
        candidate.action = ToString(node.intentBinding.action);
        candidate.hidden = node.hidden;
        candidate.offscreen = node.offscreen;
        candidate.collapsed = node.collapsed;
        candidate.requiresReveal = node.revealStrategy.required;

        bool include = false;
        if (mode == AiFilterMode::Visible) {
            include = IsVisibleNode(node);
            candidate.score = include ? 1.0 : 0.0;
        } else if (mode == AiFilterMode::Relevant) {
            include = IsInteractiveNode(node);
            candidate.score = ComputeRelevantScore(node, goalTokens, domain);
            include = include && candidate.score > 0.0;
        } else {
            include = IsInteractiveNode(node);
            candidate.score = IsVisibleNode(node) ? 1.0 : 0.75;
        }

        if (include) {
            filtered.push_back(std::move(candidate));
        }
    }

    std::sort(filtered.begin(), filtered.end(), [](const AiFilteredNode& left, const AiFilteredNode& right) {
        if (std::abs(left.score - right.score) <= 0.0001) {
            return left.nodeId < right.nodeId;
        }
        return left.score > right.score;
    });

    if (filtered.size() > topN) {
        filtered.resize(topN);
    }

    return filtered;
}

std::string SerializeAiFilteredNodesJson(const std::vector<AiFilteredNode>& nodes) {
    std::ostringstream json;
    json << "[";
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (index > 0) {
            json << ",";
        }

        const AiFilteredNode& node = nodes[index];
        json << "{";
        json << "\"node_id\":\"" << EscapeJson(node.nodeId) << "\",";
        json << "\"label\":\"" << EscapeJson(node.label) << "\",";
        json << "\"action\":\"" << EscapeJson(node.action) << "\",";
        json << "\"score\":" << node.score << ",";
        json << "\"hidden\":" << (node.hidden ? "true" : "false") << ",";
        json << "\"offscreen\":" << (node.offscreen ? "true" : "false") << ",";
        json << "\"collapsed\":" << (node.collapsed ? "true" : "false") << ",";
        json << "\"requires_reveal\":" << (node.requiresReveal ? "true" : "false");
        json << "}";
    }
    json << "]";
    return json.str();
}

LatencyBreakdownSample BuildPerfActivationSample(double targetBudgetMs, std::uint64_t frameHint) {
    const double boundedTarget = std::max(1.0, targetBudgetMs);

    LatencyBreakdownSample sample;
    sample.frame = frameHint;
    sample.traceId = "perf_activation_seed";
    sample.observationMs = boundedTarget * 0.22;
    sample.perceptionMs = boundedTarget * 0.18;
    sample.queueWaitMs = boundedTarget * 0.05;
    sample.executionMs = boundedTarget * 0.38;
    sample.verificationMs = boundedTarget * 0.12;
    sample.totalMs = sample.observationMs + sample.perceptionMs + sample.queueWaitMs + sample.executionMs + sample.verificationMs;
    sample.timestamp = std::chrono::system_clock::now();
    return sample;
}

ReflexSafetyPolicy BuildReflexSafetyPolicy(const PermissionPolicy& policy) {
    ReflexSafetyPolicy safety;
    safety.allowExecute = policy.allow_execute;
    safety.allowFileOps = policy.allow_file_ops;
    safety.allowSystemChanges = policy.allow_system_changes;
    safety.allowExploration = policy.allow_execute;
    return safety;
}

std::string ToString(ControlPriority priority) {
    switch (priority) {
    case ControlPriority::High:
        return "high";
    case ControlPriority::Medium:
        return "medium";
    case ControlPriority::Low:
    default:
        return "low";
    }
}

ControlPriority ParseControlPriorityOrDefault(const std::string& value, ControlPriority fallback) {
    const std::string normalized = ToAsciiLower(value);
    if (normalized == "high") {
        return ControlPriority::High;
    }
    if (normalized == "medium") {
        return ControlPriority::Medium;
    }
    if (normalized == "low") {
        return ControlPriority::Low;
    }
    return fallback;
}

std::vector<std::string> ParsePreferredActions(const std::string& value) {
    std::vector<std::string> actions;
    if (value.empty()) {
        return actions;
    }

    std::string token;
    for (const char ch : value) {
        if (ch == ',' || ch == ';' || ch == '|' || ch == ' ') {
            if (!token.empty()) {
                actions.push_back(ToAsciiLower(token));
                token.clear();
            }
            continue;
        }
        token.push_back(ch);
    }
    if (!token.empty()) {
        actions.push_back(ToAsciiLower(token));
    }

    std::sort(actions.begin(), actions.end());
    actions.erase(std::unique(actions.begin(), actions.end()), actions.end());
    return actions;
}

bool ExtractJsonValueStart(std::string_view payload, const std::string& key, std::size_t* valueStart) {
    if (valueStart == nullptr) {
        return false;
    }

    const std::string marker = "\"" + key + "\"";
    const std::size_t keyPos = payload.find(marker);
    if (keyPos == std::string::npos) {
        return false;
    }

    const std::size_t colonPos = payload.find(':', keyPos + marker.size());
    if (colonPos == std::string::npos) {
        return false;
    }

    std::size_t index = colonPos + 1U;
    SkipWhitespace(payload, &index);
    if (index >= payload.size()) {
        return false;
    }

    *valueStart = index;
    return true;
}

bool ExtractJsonStringField(std::string_view payload, const std::string& key, std::string* output) {
    if (output == nullptr) {
        return false;
    }

    std::size_t index = 0;
    if (!ExtractJsonValueStart(payload, key, &index)) {
        return false;
    }

    return ParseJsonString(payload, &index, output);
}

bool ExtractJsonBoolField(std::string_view payload, const std::string& key, bool* output) {
    if (output == nullptr) {
        return false;
    }

    std::size_t index = 0;
    if (!ExtractJsonValueStart(payload, key, &index)) {
        return false;
    }

    if (payload.compare(index, 4U, "true") == 0) {
        *output = true;
        return true;
    }
    if (payload.compare(index, 5U, "false") == 0) {
        *output = false;
        return true;
    }

    std::string token;
    while (index < payload.size()) {
        const char ch = payload[index];
        if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
            break;
        }
        token.push_back(ch);
        ++index;
    }

    return ParseBoolString(token, output);
}

bool ExtractJsonStringArrayField(std::string_view payload, const std::string& key, std::vector<std::string>* output) {
    if (output == nullptr) {
        return false;
    }

    std::size_t index = 0;
    if (!ExtractJsonValueStart(payload, key, &index)) {
        return false;
    }

    if (index >= payload.size() || payload[index] != '[') {
        return false;
    }
    ++index;

    std::vector<std::string> values;
    while (index < payload.size()) {
        SkipWhitespace(payload, &index);
        if (index >= payload.size()) {
            return false;
        }

        if (payload[index] == ']') {
            ++index;
            *output = std::move(values);
            return true;
        }

        std::string entry;
        if (!ParseJsonString(payload, &index, &entry)) {
            return false;
        }
        values.push_back(ToAsciiLower(entry));

        SkipWhitespace(payload, &index);
        if (index < payload.size() && payload[index] == ',') {
            ++index;
            continue;
        }
    }

    return false;
}

bool ParseGoalPayload(
    std::string_view payload,
    ReflexGoal* goal,
    bool* clear,
    std::string* errorMessage) {
    if (goal == nullptr || clear == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "Goal payload output cannot be null";
        }
        return false;
    }

    std::map<std::string, std::string> flatPayload;
    std::string flatError;
    if (ParseFlatJsonObject(payload, &flatPayload, &flatError)) {
        bool clearValue = false;
        ParseBoolString(ReadPayloadValue(flatPayload, {"clear", "reset"}), &clearValue);
        *clear = clearValue;

        if (clearValue) {
            *goal = ReflexGoal{};
            goal->updatedAtMs = EpochMs(std::chrono::system_clock::now());
            return true;
        }

        const std::string goalText = ReadPayloadValue(flatPayload, {"goal", "objective"});
        if (goalText.empty()) {
            if (errorMessage != nullptr) {
                *errorMessage = "Missing required field: goal";
            }
            return false;
        }

        goal->goal = goalText;
        goal->target = ReadPayloadValue(flatPayload, {"target", "target_hint"});
        goal->domain = ReadPayloadValue(flatPayload, {"domain"});
        goal->preferredActions = ParsePreferredActions(
            ReadPayloadValue(flatPayload, {"preferred_actions", "preferredActions", "actions"}));
        goal->active = true;
        ParseBoolString(ReadPayloadValue(flatPayload, {"active"}), &goal->active);
        goal->updatedAtMs = EpochMs(std::chrono::system_clock::now());
        return true;
    }

    bool clearValue = false;
    const bool hasClear = ExtractJsonBoolField(payload, "clear", &clearValue) ||
        ExtractJsonBoolField(payload, "reset", &clearValue);
    *clear = hasClear && clearValue;

    if (*clear) {
        *goal = ReflexGoal{};
        goal->updatedAtMs = EpochMs(std::chrono::system_clock::now());
        return true;
    }

    std::string goalText;
    if (!ExtractJsonStringField(payload, "goal", &goalText) &&
        !ExtractJsonStringField(payload, "objective", &goalText)) {
        if (errorMessage != nullptr) {
            *errorMessage = "Missing required field: goal";
        }
        return false;
    }

    std::string target;
    ExtractJsonStringField(payload, "target", &target);
    if (target.empty()) {
        ExtractJsonStringField(payload, "target_hint", &target);
    }

    std::string domain;
    ExtractJsonStringField(payload, "domain", &domain);

    std::vector<std::string> preferredActions;
    if (!ExtractJsonStringArrayField(payload, "preferred_actions", &preferredActions) &&
        !ExtractJsonStringArrayField(payload, "actions", &preferredActions) &&
        !ExtractJsonStringArrayField(payload, "preferredActions", &preferredActions)) {
        std::string preferredActionsRaw;
        if (ExtractJsonStringField(payload, "preferred_actions", &preferredActionsRaw) ||
            ExtractJsonStringField(payload, "actions", &preferredActionsRaw) ||
            ExtractJsonStringField(payload, "preferredActions", &preferredActionsRaw)) {
            preferredActions = ParsePreferredActions(preferredActionsRaw);
        }
    }

    bool active = true;
    ExtractJsonBoolField(payload, "active", &active);

    goal->goal = goalText;
    goal->target = target;
    goal->domain = domain;
    goal->preferredActions = std::move(preferredActions);
    goal->active = active;
    goal->updatedAtMs = EpochMs(std::chrono::system_clock::now());
    return true;
}

std::filesystem::path UrePersistenceDirectory() {
    return std::filesystem::path("artifacts") / "reflex";
}

std::filesystem::path UreGoalStatePath() {
    return UrePersistenceDirectory() / "goal_state_v3_2.json";
}

std::filesystem::path UreExperienceStatePath() {
    return UrePersistenceDirectory() / "experience_state_v3_2.tsv";
}

std::string SanitizePersistenceField(std::string value) {
    std::replace(value.begin(), value.end(), '\t', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value;
}

std::vector<std::string> SplitByTab(const std::string& value) {
    std::vector<std::string> fields;
    std::string token;
    for (const char ch : value) {
        if (ch == '\t') {
            fields.push_back(token);
            token.clear();
            continue;
        }
        token.push_back(ch);
    }
    fields.push_back(token);
    return fields;
}

std::string SerializeExperienceEntryLine(const ExperienceEntry& entry) {
    std::ostringstream stream;
    stream << entry.timestampMs << '\t'
           << entry.state.signature << '\t'
           << entry.state.objects << '\t'
           << entry.state.relationships << '\t'
           << SanitizePersistenceField(entry.action.objectId) << '\t'
           << SanitizePersistenceField(entry.action.objectType) << '\t'
           << SanitizePersistenceField(entry.action.action) << '\t'
           << SanitizePersistenceField(entry.action.targetLabel) << '\t'
           << SanitizePersistenceField(entry.action.reason) << '\t'
           << entry.action.priority << '\t'
           << (entry.action.executable ? 1 : 0) << '\t'
           << (entry.action.exploratory ? 1 : 0) << '\t'
           << entry.reward;
    return stream.str();
}

bool ParseBoolInt(const std::string& value) {
    return value == "1" || ToAsciiLower(value) == "true";
}

bool ParseFloat(const std::string& value, float* output) {
    if (output == nullptr) {
        return false;
    }

    std::istringstream stream(value);
    float parsed = 0.0F;
    stream >> parsed;
    if (stream.fail()) {
        return false;
    }
    *output = parsed;
    return true;
}

bool ParseExperienceEntryLine(const std::string& line, ExperienceEntry* entry) {
    if (entry == nullptr || line.empty()) {
        return false;
    }

    const std::vector<std::string> fields = SplitByTab(line);
    if (fields.size() < 13U) {
        return false;
    }

    ExperienceEntry parsed;

    const auto parseI64 = [](const std::string& value, std::int64_t* output) {
        std::int64_t local = 0;
        const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), local);
        if (error != std::errc() || ptr != value.data() + value.size()) {
            return false;
        }
        *output = local;
        return true;
    };

    const auto parseU64 = [](const std::string& value, std::uint64_t* output) {
        std::uint64_t local = 0;
        const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), local);
        if (error != std::errc() || ptr != value.data() + value.size()) {
            return false;
        }
        *output = local;
        return true;
    };

    const auto parseSize = [](const std::string& value, std::size_t* output) {
        std::uint64_t local = 0;
        const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), local);
        if (error != std::errc() || ptr != value.data() + value.size()) {
            return false;
        }
        *output = static_cast<std::size_t>(local);
        return true;
    };

    parseI64(fields[0], &parsed.timestampMs);
    parseU64(fields[1], &parsed.state.signature);
    parseSize(fields[2], &parsed.state.objects);
    parseSize(fields[3], &parsed.state.relationships);
    parsed.action.objectId = fields[4];
    parsed.action.objectType = fields[5];
    parsed.action.action = fields[6];
    parsed.action.targetLabel = fields[7];
    parsed.action.reason = fields[8];
    ParseFloat(fields[9], &parsed.action.priority);
    parsed.action.executable = ParseBoolInt(fields[10]);
    parsed.action.exploratory = ParseBoolInt(fields[11]);
    ParseFloat(fields[12], &parsed.reward);

    *entry = std::move(parsed);
    return true;
}

bool IsGoalConditionedReason(const std::string& reason) {
    return reason.rfind("goal_conditioned_", 0) == 0;
}

ControlPriority DecidePriorityForReflex(const ReflexDecision& decision, bool autoPriority, ControlPriority configuredPriority) {
    if (!autoPriority) {
        return configuredPriority;
    }

    if (decision.priority >= 0.85F) {
        return ControlPriority::High;
    }
    if (decision.priority >= 0.62F) {
        return ControlPriority::Medium;
    }
    return ControlPriority::Low;
}

Intent BuildIntentFromReflexDecision(const ReflexDecision& decision, std::uint64_t frame, ControlPriority priority) {
    Action action;
    action.name = decision.reason.empty() ? "reflex_action" : decision.reason;
    action.intentAction = decision.action;

    Intent intent;
    intent.id = "ure-runtime-" + std::to_string(frame) + "-" + decision.objectId + "-meta";
    intent.action = IntentActionFromString(action.intentAction);
    intent.name = ToString(intent.action);
    intent.target.type = TargetType::UiElement;
    intent.target.label = Wide(decision.targetLabel.empty() ? decision.objectId : decision.targetLabel);
    intent.params.values["control_priority"] = Wide(ToString(priority));
    intent.params.values["ure_object_id"] = Wide(decision.objectId);
    intent.params.values["ure_reason"] = Wide(decision.reason);
    intent.params.values["ure_exploratory"] = decision.exploratory ? L"true" : L"false";
    intent.params.values["ure_action_name"] = Wide(action.name);
    intent.params.values["ure_bundle_source"] = L"meta_policy";
    intent.source = "ure-runtime";
    intent.confidence = std::clamp(decision.priority, 0.0F, 1.0F);
    intent.constraints.timeoutMs = 12;
    intent.constraints.maxRetries = 0;
    intent.constraints.allowFallback = false;
    return intent;
}

Intent BuildIntentFromDiscreteAction(
    const Action& action,
    std::uint64_t frame,
    std::size_t index,
    ControlPriority priority,
    const std::string& objectId,
    const std::string& targetLabel,
    const std::string& reason,
    const std::string& source) {
    Intent intent;
    intent.id = "ure-runtime-" + std::to_string(frame) + "-" + std::to_string(index) + "-" + action.name;
    intent.action = IntentActionFromString(action.intentAction);
    intent.name = ToString(intent.action);
    intent.target.type = TargetType::UiElement;
    intent.target.label = Wide(targetLabel.empty() ? objectId : targetLabel);
    intent.params.values["control_priority"] = Wide(ToString(priority));
    intent.params.values["ure_object_id"] = Wide(objectId);
    intent.params.values["ure_reason"] = Wide(reason);
    intent.params.values["ure_exploratory"] = L"false";
    intent.params.values["ure_action_name"] = Wide(action.name);
    intent.params.values["ure_bundle_source"] = Wide(source);
    intent.source = "ure-runtime";
    intent.confidence = 0.9F;
    intent.constraints.timeoutMs = 12;
    intent.constraints.maxRetries = 0;
    intent.constraints.allowFallback = false;
    return intent;
}

Intent BuildIntentFromContinuousAction(
    const ContinuousAction& action,
    std::uint64_t frame,
    ControlPriority priority,
    const std::string& objectId,
    const std::string& reason) {
    Intent intent;
    intent.id = "ure-runtime-" + std::to_string(frame) + "-continuous";
    intent.action = IntentAction::Move;
    intent.name = ToString(intent.action);
    intent.target.type = TargetType::UiElement;
    intent.target.label = Wide(objectId);
    intent.params.values["control_priority"] = Wide(ToString(priority));
    intent.params.values["ure_object_id"] = Wide(objectId);
    intent.params.values["ure_reason"] = Wide(reason);
    intent.params.values["ure_exploratory"] = L"false";
    intent.params.values["ure_bundle_source"] = L"continuous_controller";
    intent.params.values["move_x"] = Wide(std::to_string(action.move_x));
    intent.params.values["move_y"] = Wide(std::to_string(action.move_y));
    intent.params.values["aim_dx"] = Wide(std::to_string(action.aim_dx));
    intent.params.values["aim_dy"] = Wide(std::to_string(action.aim_dy));
    intent.params.values["look_dx"] = Wide(std::to_string(action.look_dx));
    intent.params.values["look_dy"] = Wide(std::to_string(action.look_dy));
    intent.params.values["fire"] = action.fire ? L"true" : L"false";
    intent.params.values["interact"] = action.interact ? L"true" : L"false";
    intent.source = "ure-runtime";
    intent.confidence = 0.80F;
    intent.constraints.timeoutMs = 12;
    intent.constraints.maxRetries = 0;
    intent.constraints.allowFallback = false;
    return intent;
}

bool HasMeaningfulContinuous(const ContinuousAction& action) {
    return std::abs(action.move_x) > 0.05F ||
        std::abs(action.move_y) > 0.05F ||
        std::abs(action.aim_dx) > 0.05F ||
        std::abs(action.aim_dy) > 0.05F ||
        std::abs(action.look_dx) > 0.05F ||
        std::abs(action.look_dy) > 0.05F ||
        action.fire ||
        action.interact;
}

ReflexBundle BuildBundleFromReflexDecision(const ReflexDecision& decision) {
    ReflexBundle bundle;
    bundle.source = "meta_policy";
    bundle.target_object_id = decision.objectId;
    bundle.priority = decision.priority;

    if (!decision.action.empty()) {
        Action action;
        action.name = decision.reason.empty() ? "reflex_action" : decision.reason;
        action.intentAction = decision.action;
        bundle.discrete_actions.push_back(std::move(action));
    }

    ContinuousAction continuous;
    continuous.look_dx = std::clamp((decision.priority - 0.5F) * 0.5F, -1.0F, 1.0F);
    if (decision.exploratory) {
        continuous.look_dy = 0.12F;
    }
    bundle.continuous_actions.push_back(continuous);
    return bundle;
}

std::vector<ReflexBundle> BuildSpecialistBundles(
    const WorldModel& model,
    const AttentionMap& attention,
    const ReflexGoal* goal,
    const ReflexSafetyPolicy& safety,
    const MovementAgent& movementAgent,
    const AimAgent& aimAgent,
    const InteractionAgent& interactionAgent,
    const StrategyAgent& strategyAgent) {
    auto movementTask = std::async(std::launch::async, [&]() {
        return movementAgent.Propose(model, attention, goal, safety);
    });

    auto aimTask = std::async(std::launch::async, [&]() {
        return aimAgent.Propose(model, attention, goal, safety);
    });

    auto interactionTask = std::async(std::launch::async, [&]() {
        return interactionAgent.Propose(model, attention, goal, safety);
    });

    auto strategyTask = std::async(std::launch::async, [&]() {
        return strategyAgent.Propose(model, attention, goal, safety);
    });

    std::vector<ReflexBundle> bundles;
    bundles.reserve(4U);

    auto appendIfNonEmpty = [&bundles](ReflexBundle bundle) {
        if (!bundle.discrete_actions.empty() || !bundle.continuous_actions.empty()) {
            bundles.push_back(std::move(bundle));
        }
    };

    appendIfNonEmpty(movementTask.get());
    appendIfNonEmpty(aimTask.get());
    appendIfNonEmpty(interactionTask.get());
    appendIfNonEmpty(strategyTask.get());

    return bundles;
}

class UreDecisionProvider final : public DecisionProvider {
public:
    struct RuntimeSnapshot {
        bool active{false};
        bool executeActions{false};
        bool autoPriority{true};
        std::int64_t decisionBudgetUs{1000};
        ControlPriority configuredPriority{ControlPriority::Medium};
        ReflexGoal goal;
    };

    using RuntimeSnapshotFn = std::function<RuntimeSnapshot()>;
    using StepObserverFn = std::function<void(
        const ReflexStepResult&,
        const AttentionMap&,
        const std::vector<PredictedState>&,
        const std::vector<ReflexBundle>&,
        const CoordinatedOutput&,
        const std::vector<Skill>&,
        const std::vector<SkillNode>&,
        const AnticipationSignal&,
        const TemporalStrategyPlan&,
        const PreemptionDecision&,
        bool intentProduced,
        bool goalConditioned)>;

    UreDecisionProvider(
        UniversalReflexAgent* reflexAgent,
        std::mutex* reflexMutex,
        Telemetry* telemetry,
                SkillMemoryStore* skillMemoryStore,
        RuntimeSnapshotFn runtimeSnapshotFn,
        StepObserverFn stepObserverFn)
        : reflexAgent_(reflexAgent),
          reflexMutex_(reflexMutex),
          telemetry_(telemetry),
                    skillMemoryStore_(skillMemoryStore),
          runtimeSnapshotFn_(std::move(runtimeSnapshotFn)),
          stepObserverFn_(std::move(stepObserverFn)) {}

    std::string Name() const override {
        return "ure_realtime";
    }

    std::vector<Intent> Decide(const StateSnapshot& state, std::chrono::milliseconds, std::string* diagnostics) override {
        if (reflexAgent_ == nullptr || reflexMutex_ == nullptr || telemetry_ == nullptr || runtimeSnapshotFn_ == nullptr) {
            if (diagnostics != nullptr) {
                *diagnostics = "ure decision provider not configured";
            }
            return {};
        }

        const RuntimeSnapshot runtime = runtimeSnapshotFn_();
        if (!runtime.active) {
            if (diagnostics != nullptr) {
                *diagnostics = "ure runtime inactive";
            }
            return {};
        }

        ReflexSafetyPolicy safety = BuildReflexSafetyPolicy(PermissionPolicyStore::Get());
        safety.allowExecute = safety.allowExecute && runtime.executeActions;

        ReflexStepResult step;
        {
            std::lock_guard<std::mutex> lock(*reflexMutex_);
            step = reflexAgent_->Step(
                state,
                safety,
                std::clamp<std::int64_t>(runtime.decisionBudgetUs, 100LL, 20000LL),
                runtime.goal.active ? &runtime.goal : nullptr);
        }

        const AttentionMap attention = BuildAttentionMap(step.worldModel, 5U);
        const std::vector<PredictedState> predictions = BuildPredictedStates(step.worldModel, previousCenters_, 3U);
        previousCenters_ = BuildObjectCenters(step.worldModel);

        std::vector<ReflexBundle> bundles = BuildSpecialistBundles(
            step.worldModel,
            attention,
            runtime.goal.active ? &runtime.goal : nullptr,
            safety,
            movementAgent_,
            aimAgent_,
            interactionAgent_,
            strategyAgent_);
        bundles.push_back(BuildBundleFromReflexDecision(step.decision));

        std::vector<ReflexBundle> plannedBundles = microPlanner_.refine(step.worldModel, runtime.goal, bundles);

        std::vector<Skill> rankedSkills;
        std::vector<SkillNode> skillHierarchy;
        if (skillMemoryStore_ != nullptr) {
            rankedSkills = skillMemoryStore_->RankSkillsForGoal(runtime.goal.active ? &runtime.goal : nullptr, 8U);
            skillHierarchy = skillMemoryStore_->BuildHierarchy(16U);
        }

        const AnticipationSignal anticipation = BuildAnticipationSignal(step.worldModel, attention, predictions, 3U);
        const TemporalStrategyPlan strategy = BuildTemporalStrategy(
            runtime.goal.active ? &runtime.goal : nullptr,
            rankedSkills,
            attention,
            anticipation);
        const PreemptionDecision preemption = EvaluatePreemption(
            strategy,
            anticipation,
            step.decision,
            plannedBundles);

        if (preemption.should_preempt && !preemption.suggested_source.empty()) {
            for (ReflexBundle& bundle : plannedBundles) {
                if (bundle.source == preemption.suggested_source) {
                    bundle.priority = std::clamp(bundle.priority + 0.18F, 0.0F, 1.0F);
                }
            }
            std::sort(plannedBundles.begin(), plannedBundles.end(), [](const ReflexBundle& left, const ReflexBundle& right) {
                if (std::abs(left.priority - right.priority) > 0.0001F) {
                    return left.priority > right.priority;
                }
                if (left.source != right.source) {
                    return left.source < right.source;
                }
                return left.target_object_id < right.target_object_id;
            });
        }

        CoordinatedOutput coordinated = coordinator_.resolve(plannedBundles);
        coordinated.continuous = continuousController_.Apply(coordinated.continuous);

        const bool goalConditioned = IsGoalConditionedReason(step.decision.reason);
        bool intentProduced = false;
        std::vector<Intent> intents;
        std::string primaryObjectId = step.decision.objectId;
        std::string primaryTargetLabel = step.decision.targetLabel;
        std::string primarySource = "meta_policy";

        for (const ReflexBundle& bundle : plannedBundles) {
            if (!bundle.target_object_id.empty()) {
                primaryObjectId = bundle.target_object_id;
                if (primaryTargetLabel.empty()) {
                    primaryTargetLabel = bundle.target_object_id;
                }
                primarySource = bundle.source;
                break;
            }
        }

        if (runtime.executeActions) {
            const ControlPriority priority = DecidePriorityForReflex(
                step.decision,
                runtime.autoPriority,
                runtime.configuredPriority);

            if (HasMeaningfulContinuous(coordinated.continuous)) {
                intents.push_back(BuildIntentFromContinuousAction(
                    coordinated.continuous,
                    state.sequence,
                    priority,
                    primaryObjectId,
                    step.decision.reason));
            }

            for (std::size_t index = 0; index < coordinated.discrete.size(); ++index) {
                const Action& action = coordinated.discrete[index];
                const IntentAction mappedAction = IntentActionFromString(action.intentAction);
                if (mappedAction == IntentAction::Unknown) {
                    continue;
                }

                Intent intent = BuildIntentFromDiscreteAction(
                    action,
                    state.sequence,
                    index,
                    priority,
                    primaryObjectId,
                    primaryTargetLabel,
                    step.decision.reason,
                    primarySource);
                intent.action = mappedAction;
                intent.name = ToString(intent.action);
                intent.confidence = std::clamp(step.decision.priority, 0.0F, 1.0F);
                intents.push_back(std::move(intent));
            }

            intentProduced = !intents.empty();

            if (diagnostics != nullptr) {
                *diagnostics = intentProduced
                    ? "ure coordinated bundles produced intents"
                    : "ure coordinated output not executable";
            }
        } else if (diagnostics != nullptr) {
            *diagnostics = "ure runtime execute_actions disabled";
        }

        ReflexTelemetrySample sample;
        sample.frame = state.sequence;
        sample.decisionTimeUs = step.decisionTimeUs;
        sample.loopTimeUs = step.loopTimeUs;
        sample.priority = step.decision.priority;
        sample.decisionWithinBudget = step.decisionWithinBudget;
        sample.exploratory = step.decision.exploratory;
        sample.executable = !coordinated.discrete.empty() || HasMeaningfulContinuous(coordinated.continuous);
        sample.intentProduced = intentProduced;
        sample.goalConditioned = goalConditioned;
        sample.reason = "bundles=" + std::to_string(plannedBundles.size()) + "," + step.decision.reason;
        sample.timestamp = std::chrono::system_clock::now();
        telemetry_->LogReflexSample(sample);

        if (skillMemoryStore_ != nullptr && !coordinated.discrete.empty()) {
            skillMemoryStore_->Record("bundle_" + primarySource, coordinated.discrete, false);
        }

        if (stepObserverFn_ != nullptr) {
            stepObserverFn_(
                step,
                attention,
                predictions,
                plannedBundles,
                coordinated,
                rankedSkills,
                skillHierarchy,
                anticipation,
                strategy,
                preemption,
                intentProduced,
                goalConditioned);
        }

        return intents;
    }

private:
    UniversalReflexAgent* reflexAgent_{nullptr};
    std::mutex* reflexMutex_{nullptr};
    Telemetry* telemetry_{nullptr};
    SkillMemoryStore* skillMemoryStore_{nullptr};
    RuntimeSnapshotFn runtimeSnapshotFn_;
    StepObserverFn stepObserverFn_;
    MovementAgent movementAgent_;
    AimAgent aimAgent_;
    InteractionAgent interactionAgent_;
    StrategyAgent strategyAgent_;
    MicroPlanner microPlanner_;
    ActionCoordinator coordinator_;
    ContinuousController continuousController_;
    std::unordered_map<std::string, Vec2> previousCenters_;
};

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
            startedAt_(std::chrono::steady_clock::now()),
            skillMemoryStore_(SkillMemoryStore()) {
        RestoreUrePersistentState();
}

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

bool IntentApiServer::RestoreUrePersistentState() {
    bool restored = true;

    if (!skillMemoryStore_.Load()) {
        restored = false;
    }

    {
        std::error_code error;
        const std::filesystem::path goalPath = UreGoalStatePath();
        if (std::filesystem::exists(goalPath, error)) {
            std::ifstream stream(goalPath);
            if (stream.good()) {
                std::string payload((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                ReflexGoal goal;
                bool clear = false;
                std::string parseError;
                if (ParseGoalPayload(payload, &goal, &clear, &parseError)) {
                    std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
                    ureRuntime_.goal = clear ? ReflexGoal{} : goal;
                    ureRuntime_.goalVersion = ureRuntime_.goal.active || !ureRuntime_.goal.goal.empty() ? 1U : 0U;
                } else {
                    restored = false;
                }
            } else {
                restored = false;
            }
        }
    }

    {
        std::error_code error;
        const std::filesystem::path experiencePath = UreExperienceStatePath();
        if (std::filesystem::exists(experiencePath, error)) {
            std::ifstream stream(experiencePath);
            if (stream.good()) {
                std::vector<ExperienceEntry> entries;
                std::string line;
                while (std::getline(stream, line)) {
                    ExperienceEntry entry;
                    if (ParseExperienceEntryLine(line, &entry)) {
                        entries.push_back(std::move(entry));
                    }
                }

                if (!entries.empty()) {
                    std::lock_guard<std::mutex> lock(reflexMutex_);
                    reflexAgent_.RestoreExperience(entries);
                }
            } else {
                restored = false;
            }
        }
    }

    return restored;
}

void IntentApiServer::PersistUrePersistentState() {
    ReflexGoal goal;
    {
        std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
        goal = ureRuntime_.goal;
    }

    std::vector<ExperienceEntry> experience;
    {
        std::lock_guard<std::mutex> lock(reflexMutex_);
        experience = reflexAgent_.Experience(512U);
    }

    std::error_code directoryError;
    std::filesystem::create_directories(UrePersistenceDirectory(), directoryError);

    {
        std::ofstream stream(UreGoalStatePath(), std::ios::trunc);
        if (stream.good()) {
            stream << SerializeReflexGoalJson(goal);
        }
    }

    {
        std::ofstream stream(UreExperienceStatePath(), std::ios::trunc);
        if (stream.good()) {
            for (const ExperienceEntry& entry : experience) {
                stream << SerializeExperienceEntryLine(entry) << "\n";
            }
        }
    }

    skillMemoryStore_.Save();
}

std::string IntentApiServer::SerializeUreStatusJson() const {
    UreRuntimeState runtime;
    {
        std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
        runtime = ureRuntime_;
    }

    ReflexMetricsSnapshot metrics;
    {
        std::lock_guard<std::mutex> lock(reflexMutex_);
        metrics = reflexAgent_.Metrics();
    }

    const bool controlActive = controlRuntime_ != nullptr && controlRuntime_->Status().active;
    const std::vector<Skill> skills = skillMemoryStore_.Skills(8U);

    std::ostringstream json;
    json << "{";
    json << "\"active\":" << (runtime.active ? "true" : "false") << ",";
    json << "\"control_active\":" << (controlActive ? "true" : "false") << ",";
    json << "\"execute_actions\":" << (runtime.executeActions ? "true" : "false") << ",";
    json << "\"demo_mode\":" << (runtime.demoMode ? "true" : "false") << ",";
    json << "\"auto_priority\":" << (runtime.autoPriority ? "true" : "false") << ",";
    json << "\"priority\":\"" << EscapeJson(ToString(runtime.priority)) << "\",";
    json << "\"decision_budget_us\":" << runtime.decisionBudgetUs << ",";
    json << "\"frames_evaluated\":" << runtime.framesEvaluated << ",";
    json << "\"intents_produced\":" << runtime.intentsProduced << ",";
    json << "\"execution_attempts\":" << runtime.executionAttempts << ",";
    json << "\"execution_successes\":" << runtime.executionSuccesses << ",";
    json << "\"execution_failures\":" << runtime.executionFailures << ",";
    json << "\"bundle_frames\":" << runtime.bundleFrames << ",";
    json << "\"coordinated_actions\":" << runtime.coordinatedActions << ",";
    json << "\"skill_hierarchy_frames\":" << runtime.skillHierarchyFrames << ",";
    json << "\"anticipation_frames\":" << runtime.anticipationFrames << ",";
    json << "\"strategy_frames\":" << runtime.strategyFrames << ",";
    json << "\"preempted_frames\":" << runtime.preemptedFrames << ",";
    json << "\"goal_version\":" << runtime.goalVersion << ",";
    json << "\"last_reason\":\"" << EscapeJson(runtime.lastReason) << "\",";
    json << "\"last_trace_id\":\"" << EscapeJson(runtime.lastTraceId) << "\",";
    json << "\"last_decision_time_us\":" << runtime.lastDecisionTimeUs << ",";
    json << "\"last_loop_time_us\":" << runtime.lastLoopTimeUs << ",";
    json << "\"last_goal_conditioned\":" << (runtime.lastGoalConditioned ? "true" : "false") << ",";
    json << "\"started_at_ms\":" << EpochMs(runtime.startedAt) << ",";
    if (runtime.lastTickAt.time_since_epoch().count() > 0) {
        json << "\"last_tick_ms\":" << EpochMs(runtime.lastTickAt) << ",";
    } else {
        json << "\"last_tick_ms\":0,";
    }
    json << "\"goal\":" << SerializeReflexGoalJson(runtime.goal) << ",";
    json << "\"attention\":" << SerializeAttentionMapJson(runtime.attention) << ",";
    json << "\"prediction\":" << SerializePredictedStatesJson(runtime.predictions) << ",";
    json << "\"bundles\":" << SerializeReflexBundlesJson(runtime.bundles) << ",";
    json << "\"coordinated_output\":" << SerializeCoordinatedOutputJson(runtime.coordinatedOutput) << ",";
    json << "\"ranked_skills\":" << SerializeSkillsJson(runtime.rankedSkills) << ",";
    json << "\"skill_hierarchy\":" << SerializeSkillNodesJson(runtime.skillHierarchy) << ",";
    json << "\"anticipation\":" << SerializeAnticipationSignalJson(runtime.anticipation) << ",";
    json << "\"strategy\":" << SerializeTemporalStrategyJson(runtime.strategy) << ",";
    json << "\"preemption\":" << SerializePreemptionDecisionJson(runtime.preemption) << ",";
    json << "\"skills\":" << SerializeSkillsJson(skills) << ",";
    json << "\"metrics\":" << SerializeReflexMetricsJson(metrics) << ",";
    json << "\"telemetry\":" << telemetry_.SerializeReflexJson(256);
    if (controlRuntime_ != nullptr) {
        json << ",\"control_status\":" << ControlRuntime::SerializeSnapshotJson(controlRuntime_->Status());
    }
    json << "}";

    return json.str();
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
        bool ureActive = false;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            ureActive = ureRuntime_.active;
        }

        std::ostringstream json;
        json << "{";
        json << "\"status\":\"ok\",";
        json << "\"uptime_ms\":" << snapshot.uptimeMs << ",";
        json << "\"server_uptime_ms\":" << serverUptimeMs << ",";
        json << "\"total_executions\":" << snapshot.totalExecutions << ",";
        json << "\"success_rate\":" << snapshot.successRate << ",";
        json << "\"average_latency_ms\":" << snapshot.averageLatencyMs << ",";
        json << "\"control_active\":" << (controlActive ? "true" : "false") << ",";
        json << "\"ure_active\":" << (ureActive ? "true" : "false") << ",";
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

    if (method == "GET" && path == "/execution/memory") {
        const std::size_t limit = ReadQuerySize(query, "limit", 128U, 1U, 4096U);
        return BuildResponse(200, "OK", ExecutionMemoryStore::SerializeJson(limit));
    }

    if (method == "GET" && path == "/adapters") {
        const std::vector<AdapterMetadata> metadata = executionEngine_.ListAdapterMetadata();

        std::ostringstream json;
        json << "{";
        json << "\"count\":" << metadata.size() << ",";
        json << "\"adapters\":[";
        for (std::size_t index = 0; index < metadata.size(); ++index) {
            if (index > 0) {
                json << ",";
            }

            json << "{";
            json << "\"name\":\"" << EscapeJson(metadata[index].name) << "\",";
            json << "\"version\":\"" << EscapeJson(metadata[index].version) << "\",";
            json << "\"priority\":" << metadata[index].priority << ",";
            json << "\"supported_actions\":[";
            for (std::size_t actionIndex = 0; actionIndex < metadata[index].supportedActions.size(); ++actionIndex) {
                if (actionIndex > 0) {
                    json << ",";
                }
                json << "\"" << EscapeJson(metadata[index].supportedActions[actionIndex]) << "\"";
            }
            json << "]";
            json << "}";
        }
        json << "]";
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path.rfind("/trace/", 0) == 0) {
        const std::string rawTraceId = path.substr(std::string("/trace/").size());
        const std::string traceId = UrlDecode(rawTraceId);
        if (traceId.empty()) {
            return BuildErrorResponse(400, "Bad Request", "missing_trace_id", "Missing trace id");
        }

        const auto trace = telemetry_.FindTrace(traceId);
        if (!trace.has_value()) {
            return BuildErrorResponse(404, "Not Found", "trace_not_found", "Trace id was not found");
        }

        return BuildResponse(200, "OK", telemetry_.SerializeTraceJson(traceId));
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
        temporalStateEngine_.Record(state);

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

    if (method == "GET" && path == "/state/ai") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "state_ai_unavailable", "Unable to capture environment state");
        }

        EnsureUnifiedState(&state);
        temporalStateEngine_.Record(state);

        AIStateViewProjector projector;
        const AIStateView view = projector.Build(state, runtimeActive);

        const AiFilterMode filterMode = ParseAiFilterMode(query);
        const auto goalIt = query.find("goal");
        const std::string goal = goalIt == query.end() ? "" : UrlDecode(goalIt->second);
        const auto domainIt = query.find("domain");
        const TaskDomain domain = domainIt == query.end() ? TaskDomain::Generic : TaskPlanner::ParseDomain(domainIt->second);
        const std::size_t topN = ReadQuerySize(query, "top_n", ReadQuerySize(query, "limit", 12U, 1U, 64U), 1U, 64U);
        const bool includeHidden = ReadQueryBool(query, "include_hidden", true);

        const std::vector<AiFilteredNode> filteredNodes = BuildAiFilteredNodes(
            state.unifiedState.interactionGraph,
            filterMode,
            goal,
            domain,
            includeHidden,
            topN);

        std::ostringstream json;
        json << "{";
        json << "\"state\":" << AIStateViewProjector::SerializeJson(view) << ",";
        json << "\"filter\":{";
        json << "\"mode\":\"" << ToString(filterMode) << "\",";
        json << "\"goal\":\"" << EscapeJson(goal) << "\",";
        json << "\"domain\":\"" << EscapeJson(TaskPlanner::ToString(domain)) << "\",";
        json << "\"include_hidden\":" << (includeHidden ? "true" : "false") << ",";
        json << "\"top_n\":" << topN << ",";
        json << "\"returned\":" << filteredNodes.size() << ",";
        json << "\"nodes\":" << SerializeAiFilteredNodesJson(filteredNodes);
        json << "},";
        json << "\"latency\":" << telemetry_.SerializeLatencyJson(200);
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/state/history") {
        const std::size_t limit = ReadQuerySize(query, "limit", 64U, 1U, 256U);
        return BuildResponse(200, "OK", temporalStateEngine_.SerializeJson(limit));
    }

    if (method == "GET" && path == "/stream/frame") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "stream_frame_unavailable", "Unable to capture screen state");
        }

        EnsureUnifiedState(&state);
        temporalStateEngine_.Record(state);

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
        temporalStateEngine_.Record(state);

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
        temporalStateEngine_.Record(state);

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

    if (method == "GET" && path == "/perf/percentiles") {
        const std::size_t limit = ReadQuerySize(query, "limit", 200U, 1U, 4096U);
        return BuildResponse(200, "OK", telemetry_.SerializeLatencyPercentilesJson(limit));
    }

    if (method == "GET" && path == "/telemetry/reflex") {
        const std::size_t limit = ReadQuerySize(query, "limit", 256U, 1U, 4096U);
        return BuildResponse(200, "OK", telemetry_.SerializeReflexJson(limit));
    }

    if (method == "GET" && path == "/perf/frame-consistency") {
        const std::size_t limit = ReadQuerySize(query, "limit", 64U, 1U, 256U);
        const FrameConsistencyMetrics metrics = temporalStateEngine_.FrameConsistency(limit);

        std::ostringstream json;
        json << "{";
        json << "\"expected_frames\":" << metrics.expectedFrames << ",";
        json << "\"actual_frames\":" << metrics.actualFrames << ",";
        json << "\"skipped_frames\":" << metrics.skippedFrames << ",";
        json << "\"coherency_score\":" << metrics.score;
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/perf") {
        double targetBudgetMs = 16.0;
        std::uint64_t perfFrameHint = 0;
        if (controlRuntime_ != nullptr && controlRuntime_->Status().active) {
            const ControlRuntimeSnapshot status = controlRuntime_->Status();
            targetBudgetMs = static_cast<double>(std::max<std::int64_t>(1LL, status.targetFrameMs));
            perfFrameHint = status.latestSnapshotVersion;
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
        const bool strict = ReadQueryBool(query, "strict", false);
        bool sampleActivationSeeded = false;
        PerformanceContractSnapshot contract = telemetry_.PerformanceContract(targetBudgetMs, limit);

        if (strict && contract.sampleCount == 0) {
            static std::atomic<bool> seededOnce{false};
            bool expected = false;
            if (seededOnce.compare_exchange_strong(expected, true)) {
                telemetry_.LogLatencyBreakdown(BuildPerfActivationSample(targetBudgetMs, perfFrameHint));
                sampleActivationSeeded = true;
            }

            contract = telemetry_.PerformanceContract(targetBudgetMs, limit);
            if (contract.sampleCount == 0) {
                telemetry_.LogLatencyBreakdown(BuildPerfActivationSample(targetBudgetMs, perfFrameHint));
                sampleActivationSeeded = true;
                contract = telemetry_.PerformanceContract(targetBudgetMs, limit);
            }
        }

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"strict\":" << (strict ? "true" : "false") << ",";
        json << "\"strict_passed\":" << (contract.withinBudget ? "true" : "false") << ",";
        json << "\"strict_status\":\""
             << (strict ? (contract.withinBudget ? "pass" : "fail") : "disabled")
             << "\",";
        json << "\"sample_activation_seeded\":" << (sampleActivationSeeded ? "true" : "false") << ",";
        json << "\"contract\":" << telemetry_.SerializePerformanceContractJson(targetBudgetMs, limit);
        if (runtimeActive) {
            json << ",\"control_status\":" << ControlRuntime::SerializeSnapshotJson(controlRuntime_->Status());
        }
        json << "}";

        if (strict && !contract.withinBudget) {
            return BuildResponse(409, "Conflict", json.str());
        }

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/policy") {
        const PermissionPolicy policy = PermissionPolicyStore::Get();
        return BuildResponse(200, "OK", PermissionPolicyStore::SerializeJson(policy));
    }

    if (method == "POST" && path == "/policy") {
        std::map<std::string, std::string> payload;
        std::string jsonError;
        if (!ParseFlatJsonObject(body, &payload, &jsonError)) {
            return BuildErrorResponse(400, "Bad Request", "invalid_json", jsonError);
        }

        PermissionPolicy policy = PermissionPolicyStore::Get();

        bool parsedBool = false;
        const std::string allowExecute = ReadPayloadValue(payload, {"allow_execute"});
        if (!allowExecute.empty() && ParseBoolString(allowExecute, &parsedBool)) {
            policy.allow_execute = parsedBool;
        }

        const std::string allowFileOps = ReadPayloadValue(payload, {"allow_file_ops"});
        if (!allowFileOps.empty() && ParseBoolString(allowFileOps, &parsedBool)) {
            policy.allow_file_ops = parsedBool;
        }

        const std::string allowSystemChanges = ReadPayloadValue(payload, {"allow_system_changes"});
        if (!allowSystemChanges.empty() && ParseBoolString(allowSystemChanges, &parsedBool)) {
            policy.allow_system_changes = parsedBool;
        }

        policy = PermissionPolicyStore::Apply(policy);
        return BuildResponse(200, "OK", PermissionPolicyStore::SerializeJson(policy));
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

        controlRuntime_->SetDecisionProvider(nullptr, 2);
        controlRuntime_->SetExecutionObserver(nullptr);

        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            ureRuntime_.active = false;
            ureRuntime_.executeActions = false;
        }

        PersistUrePersistentState();

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

    if (method == "POST" && path == "/act") {
        ActionRequest actionRequest;
        std::string parseError;
        if (!ParseActionRequestJson(body, &actionRequest, &parseError)) {
            ActionExecutionResult parseFailure;
            parseFailure.status = "failure";
            parseFailure.traceId = telemetry_.NewTraceId();
            parseFailure.reason = parseError.empty() ? "invalid_action_request" : parseError;
            return BuildResponse(400, "Bad Request", SerializeActionExecutionResultJson(parseFailure));
        }

        ActionExecutor actionExecutor(registry_, executionEngine_, telemetry_);
        const ActionExecutionResult actionResult = actionExecutor.Act(actionRequest);
        const std::string payload = SerializeActionExecutionResultJson(actionResult);

        int statusCode = 200;
        std::string statusText = "OK";
        if (actionResult.status != "success") {
            if (actionResult.reason == "ambiguous_target") {
                statusCode = 409;
                statusText = "Conflict";
            } else if (actionResult.reason == "policy_denied") {
                statusCode = 403;
                statusText = "Forbidden";
            } else if (
                actionResult.reason == "missing_target" ||
                actionResult.reason == "missing_value" ||
                actionResult.reason == "unsupported_action") {
                statusCode = 400;
                statusText = "Bad Request";
            } else {
                statusCode = 500;
                statusText = "Internal Server Error";
            }
        }

        return BuildResponse(statusCode, statusText, payload);
    }

    if (method == "POST" && path == "/act/sequence") {
        IntentSequence sequence;
        std::string parseError;
        if (!ParseIntentSequenceJson(body, &sequence, &parseError)) {
            return BuildErrorResponse(
                400,
                "Bad Request",
                "invalid_sequence",
                parseError.empty() ? "Unable to parse sequence payload" : parseError);
        }

        IntentSequenceExecutor sequenceExecutor(registry_, executionEngine_, telemetry_);
        const IntentSequenceExecutionResult sequenceResult = sequenceExecutor.Execute(sequence, true);
        const std::string payload = SerializeIntentSequenceExecutionResultJson(sequenceResult);
        const int statusCode = sequenceResult.status == "success" ? 200 : 500;
        return BuildResponse(statusCode, statusCode == 200 ? "OK" : "Internal Server Error", payload);
    }

    if (method == "POST" && path == "/workflow/run") {
        IntentSequence sequence;
        std::string parseError;
        if (!ParseIntentSequenceJson(body, &sequence, &parseError)) {
            return BuildErrorResponse(
                400,
                "Bad Request",
                "invalid_workflow",
                parseError.empty() ? "Unable to parse workflow sequence" : parseError);
        }

        WorkflowExecutor workflowExecutor(registry_, executionEngine_, telemetry_);
        IntentSequenceExecutionResult workflowResult = workflowExecutor.runWorkflow(sequence);
        const std::string payload = SerializeIntentSequenceExecutionResultJson(workflowResult);
        const int statusCode = workflowResult.status == "success" ? 200 : 500;
        return BuildResponse(statusCode, statusCode == 200 ? "OK" : "Internal Server Error", payload);
    }

    if (method == "POST" && path == "/task/semantic") {
        SemanticTaskRequest semanticRequest;
        std::string parseError;
        if (!SemanticPlannerBridge::ParseSemanticTaskRequestJson(body, &semanticRequest, &parseError)) {
            return BuildErrorResponse(
                400,
                "Bad Request",
                "invalid_semantic_request",
                parseError.empty() ? "Unable to parse semantic request" : parseError);
        }

        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "semantic_state_unavailable", "Unable to capture environment state");
        }

        EnsureUnifiedState(&state);
        temporalStateEngine_.Record(state);

        const SemanticPlanResult semanticPlan = SemanticPlannerBridge::Plan(semanticRequest);

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"semantic\":" << SemanticPlannerBridge::SerializePlanJson(semanticPlan);

        if (!semanticPlan.sequenceGenerated) {
            TaskPlanner planner;
            const TaskPlanResult plan = planner.Plan(semanticPlan.taskRequest, state.unifiedState.interactionGraph);
            json << ",\"task_plan\":" << TaskPlanner::SerializeJson(plan);
            json << ",\"plans\":" << TaskPlanner::SerializeRankedPlansJson(plan);
        }

        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "POST" && path == "/ucp/act") {
        ActionRequest actionRequest;
        std::string parseError;
        if (!ParseActionRequestJson(body, &actionRequest, &parseError)) {
            return BuildErrorResponse(
                400,
                "Bad Request",
                "invalid_ucp_act",
                parseError.empty() ? "Unable to parse UCP action payload" : parseError);
        }

        ActionExecutor actionExecutor(registry_, executionEngine_, telemetry_);
        const ActionExecutionResult actionResult = actionExecutor.Act(actionRequest);
        const int statusCode = actionResult.status == "success" ? 200 :
            (actionResult.reason == "ambiguous_target" ? 409 :
                (actionResult.reason == "policy_denied" ? 403 : 500));
        return BuildResponse(
            statusCode,
            statusCode == 200 ? "OK" : (statusCode == 409 ? "Conflict" : (statusCode == 403 ? "Forbidden" : "Internal Server Error")),
            SerializeUcpActEnvelope(actionResult));
    }

    if (method == "GET" && path == "/ucp/state") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "ucp_state_unavailable", "Unable to capture environment state");
        }

        EnsureUnifiedState(&state);
        temporalStateEngine_.Record(state);

        AIStateViewProjector projector;
        const AIStateView view = projector.Build(state, runtimeActive);
        return BuildResponse(200, "OK", SerializeUcpStateEnvelope(view));
    }

    if (method == "POST" && path == "/ure/start") {
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

        bool executeActions = true;
        ParseBoolString(ReadPayloadValue(payload, {"execute", "execute_actions", "run"}), &executeActions);

        bool demoMode = false;
        ParseBoolString(ReadPayloadValue(payload, {"demo_mode", "demo"}), &demoMode);

        std::int64_t decisionBudgetUs = 1000;
        int parsedBudgetUs = 0;
        if (ParseInt32(ReadPayloadValue(payload, {"decision_budget_us", "decisionBudgetUs"}), &parsedBudgetUs) && parsedBudgetUs > 0) {
            decisionBudgetUs = std::clamp<std::int64_t>(parsedBudgetUs, 100, 20000);
        }

        const std::string priorityRaw = ReadPayloadValue(payload, {"priority", "control_priority"});
        const std::string normalizedPriority = ToAsciiLower(priorityRaw);
        const bool autoPriority = normalizedPriority.empty() || normalizedPriority == "auto";
        const ControlPriority configuredPriority = ParseControlPriorityOrDefault(normalizedPriority, ControlPriority::Medium);

        ControlRuntime& runtime = EnsureControlRuntime();
        bool controlStarted = false;
        std::string message;
        const ControlRuntimeSnapshot preStart = runtime.Status();
        if (!preStart.active) {
            controlStarted = runtime.Start(config, &message);
            if (!controlStarted) {
                return BuildResponse(409, "Conflict", BuildErrorBody("ure_control_start_failed", message));
            }
        } else {
            message = "control runtime already running";
        }

        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            if (!ureRuntime_.active) {
                ureRuntime_.startedAt = std::chrono::system_clock::now();
                ureRuntime_.framesEvaluated = 0;
                ureRuntime_.intentsProduced = 0;
                ureRuntime_.executionAttempts = 0;
                ureRuntime_.executionSuccesses = 0;
                ureRuntime_.executionFailures = 0;
                ureRuntime_.bundleFrames = 0;
                ureRuntime_.coordinatedActions = 0;
                ureRuntime_.skillHierarchyFrames = 0;
                ureRuntime_.anticipationFrames = 0;
                ureRuntime_.strategyFrames = 0;
                ureRuntime_.preemptedFrames = 0;
                ureRuntime_.lastTraceId.clear();
                ureRuntime_.lastReason.clear();
                ureRuntime_.lastDecisionTimeUs = 0;
                ureRuntime_.lastLoopTimeUs = 0;
                ureRuntime_.lastGoalConditioned = false;
                ureRuntime_.lastTickAt = std::chrono::system_clock::time_point{};
                ureRuntime_.attention = AttentionMap{};
                ureRuntime_.predictions.clear();
                ureRuntime_.bundles.clear();
                ureRuntime_.coordinatedOutput = CoordinatedOutput{};
                ureRuntime_.rankedSkills.clear();
                ureRuntime_.skillHierarchy.clear();
                ureRuntime_.anticipation = AnticipationSignal{};
                ureRuntime_.strategy = TemporalStrategyPlan{};
                ureRuntime_.preemption = PreemptionDecision{};
            }

            ureRuntime_.active = true;
            ureRuntime_.executeActions = executeActions;
            ureRuntime_.demoMode = demoMode;
            ureRuntime_.autoPriority = autoPriority;
            ureRuntime_.priority = configuredPriority;
            ureRuntime_.decisionBudgetUs = decisionBudgetUs;
        }

        auto provider = std::make_shared<UreDecisionProvider>(
            &reflexAgent_,
            &reflexMutex_,
            &telemetry_,
            &skillMemoryStore_,
            [this]() {
                UreDecisionProvider::RuntimeSnapshot snapshot;
                std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
                snapshot.active = ureRuntime_.active;
                snapshot.executeActions = ureRuntime_.executeActions;
                snapshot.autoPriority = ureRuntime_.autoPriority;
                snapshot.decisionBudgetUs = ureRuntime_.decisionBudgetUs;
                snapshot.configuredPriority = ureRuntime_.priority;
                snapshot.goal = ureRuntime_.goal;
                return snapshot;
            },
            [this](
                const ReflexStepResult& step,
                const AttentionMap& attention,
                const std::vector<PredictedState>& predictions,
                const std::vector<ReflexBundle>& bundles,
                const CoordinatedOutput& coordinated,
                const std::vector<Skill>& rankedSkills,
                const std::vector<SkillNode>& skillHierarchy,
                const AnticipationSignal& anticipation,
                const TemporalStrategyPlan& strategy,
                const PreemptionDecision& preemption,
                bool intentProduced,
                bool goalConditioned) {
                std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
                ++ureRuntime_.framesEvaluated;
                ++ureRuntime_.bundleFrames;
                if (!skillHierarchy.empty()) {
                    ++ureRuntime_.skillHierarchyFrames;
                }
                if (!anticipation.events.empty()) {
                    ++ureRuntime_.anticipationFrames;
                }
                if (strategy.active) {
                    ++ureRuntime_.strategyFrames;
                }
                if (preemption.should_preempt) {
                    ++ureRuntime_.preemptedFrames;
                }
                if (intentProduced) {
                    ++ureRuntime_.intentsProduced;
                }
                ureRuntime_.coordinatedActions += static_cast<std::uint64_t>(coordinated.discrete.size());
                ureRuntime_.lastDecisionTimeUs = step.decisionTimeUs;
                ureRuntime_.lastLoopTimeUs = step.loopTimeUs;
                ureRuntime_.lastReason = step.decision.reason;
                ureRuntime_.lastGoalConditioned = goalConditioned;
                ureRuntime_.lastTickAt = std::chrono::system_clock::now();
                ureRuntime_.attention = attention;
                ureRuntime_.predictions = predictions;
                ureRuntime_.bundles = bundles;
                ureRuntime_.coordinatedOutput = coordinated;
                ureRuntime_.rankedSkills = rankedSkills;
                ureRuntime_.skillHierarchy = skillHierarchy;
                ureRuntime_.anticipation = anticipation;
                ureRuntime_.strategy = strategy;
                ureRuntime_.preemption = preemption;
            });

        runtime.SetDecisionProvider(provider, static_cast<int>(std::clamp<std::int64_t>(decisionBudgetUs / 1000, 1, 50)));
        runtime.SetExecutionObserver([this](
                                       const Intent& intent,
                                       const ExecutionResult& result,
                                       const std::optional<FeedbackDelta>&,
                                       bool mismatch) {
            if (intent.source != "ure-runtime") {
                return;
            }

            ReflexDecision decision;
            decision.objectId = Narrow(intent.params.Get("ure_object_id"));
            decision.action = ToString(intent.action);
            decision.targetLabel = Narrow(intent.target.label);
            decision.reason = Narrow(intent.params.Get("ure_reason"));
            decision.exploratory = Narrow(intent.params.Get("ure_exploratory")) == "true";

            Action skillAction;
            skillAction.intentAction = ToString(intent.action);
            skillAction.name = Narrow(intent.params.Get("ure_action_name"));
            if (skillAction.name.empty()) {
                skillAction.name = skillAction.intentAction;
            }

            const std::string bundleSource = Narrow(intent.params.Get("ure_bundle_source"));
            skillMemoryStore_.Record(
                "bundle_" + (bundleSource.empty() ? std::string("unknown") : bundleSource),
                {skillAction},
                result.IsSuccess());

            {
                std::lock_guard<std::mutex> reflexLock(reflexMutex_);
                const float reward = result.IsSuccess() ? (mismatch ? 0.25F : 1.0F) : -1.0F;
                reflexAgent_.RecordExecutionOutcome(decision, result.IsSuccess(), reward);
            }

            std::lock_guard<std::mutex> runtimeLock(ureRuntimeMutex_);
            ++ureRuntime_.executionAttempts;
            if (result.IsSuccess()) {
                ++ureRuntime_.executionSuccesses;
            } else {
                ++ureRuntime_.executionFailures;
            }
            ureRuntime_.lastTraceId = result.traceId;
        });

        std::ostringstream json;
        json << "{";
        json << "\"started\":" << (controlStarted ? "true" : "false") << ",";
        json << "\"message\":\"" << EscapeJson(message) << "\",";
        json << "\"status\":" << SerializeUreStatusJson();
        json << "}";

        return BuildResponse(200, "OK", json.str());
    }

    if (method == "POST" && path == "/ure/stop") {
        if (!controlRuntime_) {
            return BuildErrorResponse(409, "Conflict", "ure_not_running", "URE runtime is not running");
        }

        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            if (!ureRuntime_.active) {
                return BuildErrorResponse(409, "Conflict", "ure_not_running", "URE runtime is not running");
            }
            ureRuntime_.active = false;
            ureRuntime_.executeActions = false;
        }

        controlRuntime_->SetDecisionProvider(nullptr, 2);
        controlRuntime_->SetExecutionObserver(nullptr);
        PersistUrePersistentState();

        std::ostringstream json;
        json << "{";
        json << "\"stopped\":true,";
        json << "\"status\":" << SerializeUreStatusJson();
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/ure/status") {
        return BuildResponse(200, "OK", SerializeUreStatusJson());
    }

    if (method == "GET" && path == "/ure/bundles") {
        std::vector<ReflexBundle> bundles;
        CoordinatedOutput coordinated;
        std::uint64_t bundleFrames = 0;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            bundles = ureRuntime_.bundles;
            coordinated = ureRuntime_.coordinatedOutput;
            bundleFrames = ureRuntime_.bundleFrames;
        }

        std::ostringstream json;
        json << "{";
        json << "\"bundle_frames\":" << bundleFrames << ",";
        json << "\"bundles\":" << SerializeReflexBundlesJson(bundles) << ",";
        json << "\"coordinated_output\":" << SerializeCoordinatedOutputJson(coordinated);
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/ure/attention") {
        AttentionMap attention;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            attention = ureRuntime_.attention;
        }
        return BuildResponse(200, "OK", SerializeAttentionMapJson(attention));
    }

    if (method == "GET" && path == "/ure/prediction") {
        std::vector<PredictedState> predictions;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            predictions = ureRuntime_.predictions;
        }
        return BuildResponse(200, "OK", SerializePredictedStatesJson(predictions));
    }

    if (method == "GET" && path == "/ure/skills/active") {
        TemporalStrategyPlan strategy;
        PreemptionDecision preemption;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            strategy = ureRuntime_.strategy;
            preemption = ureRuntime_.preemption;
        }

        std::vector<std::string> activeSkills;
        activeSkills.reserve(strategy.milestones.size());
        for (const StrategyMilestone& milestone : strategy.milestones) {
            if (!milestone.skill_name.empty()) {
                activeSkills.push_back(milestone.skill_name);
            }
        }
        std::sort(activeSkills.begin(), activeSkills.end());
        activeSkills.erase(std::unique(activeSkills.begin(), activeSkills.end()), activeSkills.end());

        std::ostringstream json;
        json << "{";
        json << "\"active\":" << (strategy.active ? "true" : "false") << ",";
        json << "\"strategy_id\":\"" << EscapeJson(strategy.strategy_id) << "\",";
        json << "\"active_skills\":[";
        for (std::size_t index = 0; index < activeSkills.size(); ++index) {
            if (index > 0) {
                json << ",";
            }
            json << "\"" << EscapeJson(activeSkills[index]) << "\"";
        }
        json << "],";
        json << "\"preemption\":" << SerializePreemptionDecisionJson(preemption);
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/ure/skills") {
        const std::size_t limit = ReadQuerySize(query, "limit", 8U, 1U, 64U);

        std::vector<Skill> rankedSkills;
        std::vector<SkillNode> hierarchy;
        ReflexGoal goal;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            rankedSkills = ureRuntime_.rankedSkills;
            hierarchy = ureRuntime_.skillHierarchy;
            goal = ureRuntime_.goal;
        }

        if (rankedSkills.empty()) {
            rankedSkills = skillMemoryStore_.RankSkillsForGoal(goal.active ? &goal : nullptr, limit);
        }
        if (hierarchy.empty()) {
            hierarchy = skillMemoryStore_.BuildHierarchy(16U);
        }

        if (rankedSkills.size() > limit) {
            rankedSkills.resize(limit);
        }

        std::ostringstream json;
        json << "{";
        json << "\"ranked_skills\":" << SerializeSkillsJson(rankedSkills) << ",";
        json << "\"skill_hierarchy\":" << SerializeSkillNodesJson(hierarchy);
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/ure/anticipation") {
        AnticipationSignal anticipation;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            anticipation = ureRuntime_.anticipation;
        }
        return BuildResponse(200, "OK", SerializeAnticipationSignalJson(anticipation));
    }

    if (method == "GET" && path == "/ure/strategy") {
        TemporalStrategyPlan strategy;
        PreemptionDecision preemption;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            strategy = ureRuntime_.strategy;
            preemption = ureRuntime_.preemption;
        }

        std::ostringstream json;
        json << "{";
        json << "\"strategy\":" << SerializeTemporalStrategyJson(strategy) << ",";
        json << "\"preemption\":" << SerializePreemptionDecisionJson(preemption);
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if ((method == "POST" && path == "/ure/goal") || (method == "GET" && path == "/ure/goal")) {
        if (method == "GET") {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            return BuildResponse(200, "OK", SerializeReflexGoalJson(ureRuntime_.goal));
        }

        ReflexGoal parsedGoal;
        bool clear = false;
        std::string parseError;
        if (!ParseGoalPayload(body, &parsedGoal, &clear, &parseError)) {
            return BuildErrorResponse(400, "Bad Request", "invalid_goal_payload", parseError.empty() ? "Invalid goal payload" : parseError);
        }

        ReflexGoal goalSnapshot;
        std::uint64_t goalVersionSnapshot = 0;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            if (clear) {
                ureRuntime_.goal = ReflexGoal{};
            } else {
                ureRuntime_.goal = parsedGoal;
            }

            ureRuntime_.goalVersion += 1;
            goalSnapshot = ureRuntime_.goal;
            goalVersionSnapshot = ureRuntime_.goalVersion;
        }

        PersistUrePersistentState();

        std::ostringstream json;
        json << "{";
        json << "\"goal_version\":" << goalVersionSnapshot << ",";
        json << "\"goal\":" << SerializeReflexGoalJson(goalSnapshot) << ",";
        json << "\"status\":" << SerializeUreStatusJson();
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/ure/world-model") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "ure_state_unavailable", "Unable to capture environment state");
        }

        EnsureUnifiedState(&state);
        temporalStateEngine_.Record(state);

        const ReflexSafetyPolicy safety = BuildReflexSafetyPolicy(PermissionPolicyStore::Get());
        ReflexGoal activeGoal;
        bool hasActiveGoal = false;
        {
            std::lock_guard<std::mutex> goalLock(ureRuntimeMutex_);
            activeGoal = ureRuntime_.goal;
            hasActiveGoal = ureRuntime_.goal.active;
        }
        ReflexStepResult step;
        {
            std::lock_guard<std::mutex> lock(reflexMutex_);
            step = reflexAgent_.Step(state, safety, 1000, hasActiveGoal ? &activeGoal : nullptr);
        }

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"world_model\":" << SerializeWorldModelJson(step.worldModel);
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/ure/affordances") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "ure_state_unavailable", "Unable to capture environment state");
        }

        EnsureUnifiedState(&state);
        temporalStateEngine_.Record(state);

        const ReflexSafetyPolicy safety = BuildReflexSafetyPolicy(PermissionPolicyStore::Get());
        ReflexGoal activeGoal;
        bool hasActiveGoal = false;
        {
            std::lock_guard<std::mutex> goalLock(ureRuntimeMutex_);
            activeGoal = ureRuntime_.goal;
            hasActiveGoal = ureRuntime_.goal.active;
        }
        ReflexStepResult step;
        {
            std::lock_guard<std::mutex> lock(reflexMutex_);
            step = reflexAgent_.Step(state, safety, 1000, hasActiveGoal ? &activeGoal : nullptr);
        }

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"affordances\":" << SerializeAffordancesJson(step.affordances);
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/ure/decision") {
        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "ure_state_unavailable", "Unable to capture environment state");
        }

        EnsureUnifiedState(&state);
        temporalStateEngine_.Record(state);

        const ReflexSafetyPolicy safety = BuildReflexSafetyPolicy(PermissionPolicyStore::Get());
        ReflexGoal activeGoal;
        bool hasActiveGoal = false;
        {
            std::lock_guard<std::mutex> goalLock(ureRuntimeMutex_);
            activeGoal = ureRuntime_.goal;
            hasActiveGoal = ureRuntime_.goal.active;
        }
        ReflexStepResult step;
        {
            std::lock_guard<std::mutex> lock(reflexMutex_);
            step = reflexAgent_.Step(state, safety, 1000, hasActiveGoal ? &activeGoal : nullptr);
        }

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"decision\":" << SerializeReflexDecisionJson(step.decision) << ",";
        json << "\"decision_time_us\":" << step.decisionTimeUs << ",";
        json << "\"decision_within_budget\":" << (step.decisionWithinBudget ? "true" : "false");
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/ure/metrics") {
        ReflexMetricsSnapshot metrics;
        {
            std::lock_guard<std::mutex> lock(reflexMutex_);
            metrics = reflexAgent_.Metrics();
        }

        std::ostringstream json;
        json << "{";
        json << "\"metrics\":" << SerializeReflexMetricsJson(metrics) << ",";
        json << "\"telemetry\":" << telemetry_.SerializeReflexJson(256) << ",";
        json << "\"runtime\":" << SerializeUreStatusJson();
        json << "}";
        return BuildResponse(200, "OK", json.str());
    }

    if (method == "GET" && path == "/ure/experience") {
        const std::size_t limit = ReadQuerySize(query, "limit", 64U, 1U, 512U);

        std::vector<ExperienceEntry> entries;
        {
            std::lock_guard<std::mutex> lock(reflexMutex_);
            entries = reflexAgent_.Experience(limit);
        }
        return BuildResponse(200, "OK", SerializeExperienceEntriesJson(entries));
    }

    if (method == "POST" && (path == "/ure/step" || path == "/ure/demo")) {
        std::map<std::string, std::string> payload;
        if (!body.empty()) {
            std::string jsonError;
            if (!ParseFlatJsonObject(body, &payload, &jsonError)) {
                return BuildErrorResponse(400, "Bad Request", "invalid_json", jsonError);
            }
        }

        bool execute = false;
        const std::string executeRaw = ReadPayloadValue(payload, {"execute", "run"});
        ParseBoolString(executeRaw, &execute);

        int decisionBudgetUs = 1000;
        const std::string budgetRaw = ReadPayloadValue(payload, {"decision_budget_us", "decisionBudgetUs"});
        int parsedBudget = 0;
        if (ParseInt32(budgetRaw, &parsedBudget) && parsedBudget > 0) {
            decisionBudgetUs = std::clamp(parsedBudget, 100, 20000);
        }

        const std::string scenario = path == "/ure/demo" ? ReadPayloadValue(payload, {"scenario"}) : "";

        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "ure_state_unavailable", "Unable to capture environment state");
        }

        EnsureUnifiedState(&state);
        temporalStateEngine_.Record(state);

        const PermissionPolicy policy = PermissionPolicyStore::Get();
        const ReflexSafetyPolicy safety = BuildReflexSafetyPolicy(policy);

        ReflexGoal activeGoal;
        bool hasActiveGoal = false;
        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            activeGoal = ureRuntime_.goal;
            hasActiveGoal = ureRuntime_.goal.active;
        }

        ReflexStepResult step;
        {
            std::lock_guard<std::mutex> lock(reflexMutex_);
            step = reflexAgent_.Step(state, safety, decisionBudgetUs, hasActiveGoal ? &activeGoal : nullptr);
        }

        const AttentionMap attention = BuildAttentionMap(step.worldModel, 5U);
        const std::vector<PredictedState> predictions = BuildPredictedStates(
            step.worldModel,
            std::unordered_map<std::string, Vec2>{},
            3U);

        MovementAgent movementAgent;
        AimAgent aimAgent;
        InteractionAgent interactionAgent;
        StrategyAgent strategyAgent;
        MicroPlanner microPlanner;
        ActionCoordinator coordinator;
        ContinuousController controller;

        std::vector<ReflexBundle> bundles = BuildSpecialistBundles(
            step.worldModel,
            attention,
            hasActiveGoal ? &activeGoal : nullptr,
            safety,
            movementAgent,
            aimAgent,
            interactionAgent,
            strategyAgent);
        bundles.push_back(BuildBundleFromReflexDecision(step.decision));
        const std::vector<ReflexBundle> plannedBundles = microPlanner.refine(step.worldModel, activeGoal, bundles);

        CoordinatedOutput coordinatedOutput = coordinator.resolve(plannedBundles);
        coordinatedOutput.continuous = controller.Apply(coordinatedOutput.continuous);

        {
            std::lock_guard<std::mutex> lock(ureRuntimeMutex_);
            ureRuntime_.attention = attention;
            ureRuntime_.predictions = predictions;
            ureRuntime_.bundles = plannedBundles;
            ureRuntime_.coordinatedOutput = coordinatedOutput;
        }

        bool executed = false;
        ActionExecutionResult actionResult;
        bool hasActionResult = false;
        std::string executionReason = "not_requested";

        if (execute) {
            if (!policy.allow_execute) {
                executionReason = "policy_denied";
            } else if (!step.decision.executable || step.decision.action.empty() || step.decision.targetLabel.empty()) {
                executionReason = "no_executable_reflex_action";
            } else {
                ActionRequest reflexRequest;
                reflexRequest.action = step.decision.action;
                reflexRequest.target = step.decision.targetLabel;
                reflexRequest.context.domain = "generic";

                ActionExecutor actionExecutor(registry_, executionEngine_, telemetry_);
                actionResult = actionExecutor.Act(reflexRequest);
                hasActionResult = true;
                executed = actionResult.status == "success";
                executionReason = executed ? "executed" : actionResult.reason;

                {
                    std::lock_guard<std::mutex> lock(reflexMutex_);
                    reflexAgent_.RecordExecutionOutcome(step.decision, executed);
                }
            }
        }

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        if (!scenario.empty()) {
            json << "\"scenario\":\"" << EscapeJson(scenario) << "\",";
        }
        json << "\"step\":" << SerializeReflexStepResultJson(step) << ",";
        json << "\"attention\":" << SerializeAttentionMapJson(attention) << ",";
        json << "\"prediction\":" << SerializePredictedStatesJson(predictions) << ",";
        json << "\"bundles\":" << SerializeReflexBundlesJson(plannedBundles) << ",";
        json << "\"coordinated_output\":" << SerializeCoordinatedOutputJson(coordinatedOutput) << ",";
        json << "\"executed\":" << (executed ? "true" : "false") << ",";
        json << "\"execution_reason\":\"" << EscapeJson(executionReason) << "\",";
        if (hasActionResult) {
            json << "\"action_result\":" << SerializeActionExecutionResultJson(actionResult);
        } else {
            json << "\"action_result\":null";
        }
        json << "}";

        const int statusCode = execute && hasActionResult && !executed ? 500 : 200;
        return BuildResponse(statusCode, statusCode == 200 ? "OK" : "Internal Server Error", json.str());
    }

    if (method == "POST" && path == "/task/plan") {
        std::map<std::string, std::string> payload;
        std::string jsonError;
        if (!ParseFlatJsonObject(body, &payload, &jsonError)) {
            return BuildErrorResponse(400, "Bad Request", "invalid_json", jsonError);
        }

        TaskRequest taskRequest;
        taskRequest.goal = ReadPayloadValue(payload, {"goal", "task", "objective"});
        if (taskRequest.goal.empty()) {
            return BuildErrorResponse(400, "Bad Request", "missing_goal", "Missing required field: goal");
        }

        taskRequest.targetHint = ReadPayloadValue(payload, {"target", "target_hint", "hint"});
        taskRequest.domain = TaskPlanner::ParseDomain(ReadPayloadValue(payload, {"domain"}));
        taskRequest.maxPlans = ParseSizeWithBounds(ReadPayloadValue(payload, {"max_plans", "limit"}), 3U, 1U, 8U);

        bool allowHidden = true;
        const std::string allowHiddenRaw = ReadPayloadValue(payload, {"allow_hidden", "include_hidden"});
        ParseBoolString(allowHiddenRaw, &allowHidden);
        taskRequest.allowHidden = allowHidden;

        bool runtimeActive = false;
        EnvironmentState state;
        if (!CaptureEnvironmentState(controlRuntime_, streamEnvironmentAdapter_, &state, &runtimeActive)) {
            return BuildErrorResponse(503, "Service Unavailable", "task_plan_state_unavailable", "Unable to capture environment state");
        }

        EnsureUnifiedState(&state);
        temporalStateEngine_.Record(state);

        TaskPlanner planner;
        const TaskPlanResult plan = planner.Plan(taskRequest, state.unifiedState.interactionGraph);

        std::ostringstream json;
        json << "{";
        json << "\"runtime_active\":" << (runtimeActive ? "true" : "false") << ",";
        json << "\"planning_only\":true,";
        json << "\"graph_version\":" << state.unifiedState.interactionGraph.version << ",";
        json << "\"task_plan\":" << TaskPlanner::SerializeJson(plan) << ",";
        json << "\"plans\":" << TaskPlanner::SerializeRankedPlansJson(plan);
        json << "}";

        return BuildResponse(200, "OK", json.str());
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

        const std::string nodeId = ReadPayloadValue(payload, {"node_id", "nodeId"});
        if (!nodeId.empty()) {
            intent.target.nodeId = nodeId;
        }

        const PermissionCheckResult policyCheck = PermissionPolicyStore::Check(intent);
        if (!policyCheck.allowed) {
            return BuildErrorResponse(403, "Forbidden", "policy_denied", policyCheck.reason);
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

        ExecutionContract contract(executionEngine_, registry_);
        const ExecutionContractResult contractResult = contract.Execute(intent, intent.target.nodeId);
        const ExecutionResult result = contractResult.execution;
        if ((result.status == ExecutionStatus::SUCCESS || result.status == ExecutionStatus::PARTIAL) && !intent.id.empty()) {
            registry_.RecordInteraction(intent.id);
        }

        std::ostringstream json;
        json << "{";
        json << "\"trace_id\":\"" << EscapeJson(result.traceId) << "\",";
        json << "\"status\":\"" << EscapeJson(ToString(result.status)) << "\",";
        json << "\"verified\":" << (result.verified ? "true" : "false") << ",";
        json << "\"used_fallback\":" << (result.usedFallback ? "true" : "false") << ",";
        json << "\"method\":\"" << EscapeJson(result.method) << "\",";
        json << "\"message\":\"" << EscapeJson(result.message) << "\",";
        json << "\"durationMs\":" << result.duration.count() << ",";
        json << "\"contract_satisfied\":" << (contractResult.contractSatisfied ? "true" : "false") << ",";
        json << "\"contract_stage\":\"" << EscapeJson(contractResult.stage) << "\",";
        json << "\"contract_message\":\"" << EscapeJson(contractResult.message) << "\",";
        json << "\"reveal_required\":" << (contractResult.revealRequired ? "true" : "false") << ",";
        json << "\"reveal_attempted\":" << (contractResult.reveal.attempted ? "true" : "false") << ",";
        json << "\"reveal_success\":" << (contractResult.reveal.success ? "true" : "false") << ",";
        json << "\"reveal_attempted_steps\":" << contractResult.reveal.attemptedSteps << ",";
        json << "\"reveal_completed_steps\":" << contractResult.reveal.completedSteps << ",";
        json << "\"reveal_total_step_attempts\":" << contractResult.reveal.totalStepAttempts << ",";
        json << "\"reveal_fallback_used\":" << (contractResult.reveal.fallbackUsed ? "true" : "false") << ",";
        json << "\"reveal_fallback_step_count\":" << contractResult.reveal.fallbackStepCount;
        json << "}";

        const int statusCode = result.status == ExecutionStatus::FAILED ? 500 : 200;
        return BuildResponse(statusCode, statusCode == 200 ? "OK" : "Internal Server Error", json.str());
    }

    return BuildErrorResponse(404, "Not Found", "route_not_found", "Route not found");
}

}  // namespace iee