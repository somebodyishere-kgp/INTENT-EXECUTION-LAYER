#include <WinSock2.h>
#include <WS2tcpip.h>

#include "IntentApiServer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>

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

}  // namespace

IntentApiServer::IntentApiServer(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry)
    : registry_(registry),
      executionEngine_(executionEngine),
      telemetry_(telemetry),
      startedAt_(std::chrono::steady_clock::now()) {}

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
        } else if (request.empty()) {
            response = BuildErrorResponse(400, "Bad Request", "empty_request", "Request is empty");
        } else {
            response = HandleRequest(request);
        }

        send(client, response.c_str(), static_cast<int>(response.size()), 0);
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

std::string IntentApiServer::HandleRequest(const std::string& request) {
    const std::size_t firstSpace = request.find(' ');
    const std::size_t secondSpace = firstSpace == std::string::npos ? std::string::npos : request.find(' ', firstSpace + 1U);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        return BuildErrorResponse(400, "Bad Request", "malformed_request_line", "Malformed request line");
    }

    const std::string method = request.substr(0, firstSpace);
    std::string path = request.substr(firstSpace + 1U, secondSpace - firstSpace - 1U);
    const std::size_t queryPos = path.find('?');
    if (queryPos != std::string::npos) {
        path = path.substr(0, queryPos);
    }

    const std::size_t bodyPos = request.find("\r\n\r\n");
    const std::string body = bodyPos == std::string::npos ? std::string() : request.substr(bodyPos + 4U);

    if (method == "GET" && path == "/health") {
        const TelemetrySnapshot snapshot = telemetry_.Snapshot();
        const auto serverUptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt_)
                                         .count();

        std::ostringstream json;
        json << "{";
        json << "\"status\":\"ok\",";
        json << "\"uptime_ms\":" << snapshot.uptimeMs << ",";
        json << "\"server_uptime_ms\":" << serverUptimeMs << ",";
        json << "\"total_executions\":" << snapshot.totalExecutions << ",";
        json << "\"success_rate\":" << snapshot.successRate << ",";
        json << "\"average_latency_ms\":" << snapshot.averageLatencyMs;
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