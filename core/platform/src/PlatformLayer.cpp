#include "PlatformLayer.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>

namespace iee {
namespace {

std::mutex g_policyMutex;
PermissionPolicy g_policy;

std::mutex g_memoryMutex;
ExecutionMemory g_memory;

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

bool IsFileIntentAction(IntentAction action) {
    return action == IntentAction::Create || action == IntentAction::Delete || action == IntentAction::Move;
}

bool IsSystemChangingAction(IntentAction action) {
    return action == IntentAction::Delete || action == IntentAction::Move;
}

std::int64_t EpochMs(std::chrono::system_clock::time_point value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
}

bool SkipWhitespace(std::string_view text, std::size_t* index) {
    if (index == nullptr) {
        return false;
    }

    while (*index < text.size()) {
        const char ch = text[*index];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++(*index);
            continue;
        }
        break;
    }

    return *index < text.size();
}

bool ParseJsonString(std::string_view text, std::size_t* index, std::string* output) {
    if (index == nullptr || output == nullptr || *index >= text.size() || text[*index] != '"') {
        return false;
    }

    ++(*index);
    std::string value;

    while (*index < text.size()) {
        const char ch = text[*index];
        ++(*index);

        if (ch == '\\') {
            if (*index >= text.size()) {
                return false;
            }

            const char escaped = text[*index];
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

bool ExtractJsonStringField(std::string_view payload, const std::string& key, std::string* output) {
    const std::string marker = "\"" + key + "\"";
    const std::size_t keyPos = payload.find(marker);
    if (keyPos == std::string::npos) {
        return false;
    }

    std::size_t index = keyPos + marker.size();
    if (!SkipWhitespace(payload, &index) || payload[index] != ':') {
        return false;
    }

    ++index;
    if (!SkipWhitespace(payload, &index)) {
        return false;
    }

    return ParseJsonString(payload, &index, output);
}

bool ExtractJsonObjectField(std::string_view payload, const std::string& key, std::string* output) {
    const std::string marker = "\"" + key + "\"";
    const std::size_t keyPos = payload.find(marker);
    if (keyPos == std::string::npos) {
        return false;
    }

    std::size_t index = keyPos + marker.size();
    if (!SkipWhitespace(payload, &index) || payload[index] != ':') {
        return false;
    }

    ++index;
    if (!SkipWhitespace(payload, &index) || payload[index] != '{') {
        return false;
    }

    const std::size_t start = index;
    std::size_t depth = 0;
    bool inString = false;

    while (index < payload.size()) {
        const char ch = payload[index];

        if (ch == '"') {
            bool escaped = false;
            std::size_t back = index;
            while (back > start && payload[back - 1] == '\\') {
                escaped = !escaped;
                --back;
            }
            if (!escaped) {
                inString = !inString;
            }
        }

        if (!inString) {
            if (ch == '{') {
                ++depth;
            } else if (ch == '}') {
                if (depth == 0) {
                    return false;
                }
                --depth;
                if (depth == 0) {
                    *output = std::string(payload.substr(start, index - start + 1U));
                    return true;
                }
            }
        }

        ++index;
    }

    return false;
}

bool ExtractJsonArrayField(std::string_view payload, const std::string& key, std::string* output) {
    const std::string marker = "\"" + key + "\"";
    const std::size_t keyPos = payload.find(marker);
    if (keyPos == std::string::npos) {
        return false;
    }

    std::size_t index = keyPos + marker.size();
    if (!SkipWhitespace(payload, &index) || payload[index] != ':') {
        return false;
    }

    ++index;
    if (!SkipWhitespace(payload, &index) || payload[index] != '[') {
        return false;
    }

    const std::size_t start = index;
    std::size_t depth = 0;
    bool inString = false;

    while (index < payload.size()) {
        const char ch = payload[index];

        if (ch == '"') {
            bool escaped = false;
            std::size_t back = index;
            while (back > start && payload[back - 1] == '\\') {
                escaped = !escaped;
                --back;
            }
            if (!escaped) {
                inString = !inString;
            }
        }

        if (!inString) {
            if (ch == '[') {
                ++depth;
            } else if (ch == ']') {
                if (depth == 0) {
                    return false;
                }
                --depth;
                if (depth == 0) {
                    *output = std::string(payload.substr(start, index - start + 1U));
                    return true;
                }
            }
        }

        ++index;
    }

    return false;
}

std::vector<std::string> ParseObjectArrayItems(const std::string& arrayJson) {
    std::vector<std::string> objects;
    if (arrayJson.size() < 2 || arrayJson.front() != '[' || arrayJson.back() != ']') {
        return objects;
    }

    std::size_t index = 1;
    while (index + 1 < arrayJson.size()) {
        while (index + 1 < arrayJson.size() &&
            (arrayJson[index] == ' ' || arrayJson[index] == '\t' || arrayJson[index] == '\r' ||
             arrayJson[index] == '\n' || arrayJson[index] == ',')) {
            ++index;
        }

        if (index + 1 >= arrayJson.size() || arrayJson[index] != '{') {
            break;
        }

        const std::size_t start = index;
        std::size_t depth = 0;
        bool inString = false;

        while (index < arrayJson.size()) {
            const char ch = arrayJson[index];

            if (ch == '"') {
                bool escaped = false;
                std::size_t back = index;
                while (back > start && arrayJson[back - 1] == '\\') {
                    escaped = !escaped;
                    --back;
                }
                if (!escaped) {
                    inString = !inString;
                }
            }

            if (!inString) {
                if (ch == '{') {
                    ++depth;
                } else if (ch == '}') {
                    --depth;
                    if (depth == 0) {
                        objects.push_back(arrayJson.substr(start, index - start + 1U));
                        ++index;
                        break;
                    }
                }
            }

            ++index;
        }
    }

    return objects;
}

std::vector<std::string> SplitPhrase(std::string value, const std::string& marker) {
    std::vector<std::string> parts;
    std::string lower = ToAsciiLower(value);

    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t found = lower.find(marker, start);
        if (found == std::string::npos) {
            const std::string tail = value.substr(start);
            if (!tail.empty()) {
                parts.push_back(tail);
            }
            break;
        }

        const std::string piece = value.substr(start, found - start);
        if (!piece.empty()) {
            parts.push_back(piece);
        }

        start = found + marker.size();
    }

    return parts;
}

bool ParseActLikePhrase(const std::string& phrase, ActionRequest* request) {
    if (request == nullptr) {
        return false;
    }

    const std::string normalized = ToAsciiLower(phrase);
    if (normalized.empty()) {
        return false;
    }

    ActionRequest parsed;

    const auto startsWith = [&normalized](const char* prefix) {
        const std::size_t length = std::char_traits<char>::length(prefix);
        return normalized.size() >= length && normalized.compare(0, length, prefix) == 0;
    };

    if (startsWith("click ") || startsWith("open ") || startsWith("activate ")) {
        parsed.action = "activate";
        const std::size_t separator = normalized.find(' ');
        parsed.target = separator == std::string::npos ? "" : phrase.substr(separator + 1U);
    } else if (startsWith("select ")) {
        parsed.action = "select";
        parsed.target = phrase.substr(std::string("select ").size());
    } else if (startsWith("type ")) {
        parsed.action = "set_value";
        const std::size_t inPos = normalized.find(" in ");
        if (inPos == std::string::npos) {
            parsed.value = phrase.substr(std::string("type ").size());
            parsed.target = "focused input";
        } else {
            parsed.value = phrase.substr(std::string("type ").size(), inPos - std::string("type ").size());
            parsed.target = phrase.substr(inPos + std::string(" in ").size());
        }
    } else if (startsWith("navigate ") || startsWith("go to ")) {
        parsed.action = "navigate";
        parsed.target = "address bar";
        parsed.value = phrase.substr(startsWith("go to ") ? std::string("go to ").size() : std::string("navigate ").size());
    } else {
        return false;
    }

    if (parsed.target.empty()) {
        return false;
    }

    *request = std::move(parsed);
    return true;
}

}  // namespace

PermissionPolicy PermissionPolicyStore::Get() {
    std::lock_guard<std::mutex> lock(g_policyMutex);
    return g_policy;
}

PermissionPolicy PermissionPolicyStore::Apply(const PermissionPolicy& policy) {
    std::lock_guard<std::mutex> lock(g_policyMutex);
    g_policy = policy;
    return g_policy;
}

PermissionCheckResult PermissionPolicyStore::Check(const Intent& intent) {
    const PermissionPolicy policy = Get();
    PermissionCheckResult result;

    if (!policy.allow_execute) {
        result.allowed = false;
        result.reason = "execute_not_allowed";
        return result;
    }

    if (IsFileIntentAction(intent.action) && !policy.allow_file_ops) {
        result.allowed = false;
        result.reason = "file_ops_not_allowed";
        return result;
    }

    if (IsSystemChangingAction(intent.action) && !policy.allow_system_changes) {
        result.allowed = false;
        result.reason = "system_changes_not_allowed";
        return result;
    }

    result.allowed = true;
    result.reason = "allowed";
    return result;
}

std::string PermissionPolicyStore::SerializeJson(const PermissionPolicy& policy) {
    std::ostringstream json;
    json << "{";
    json << "\"allow_execute\":" << (policy.allow_execute ? "true" : "false") << ",";
    json << "\"allow_file_ops\":" << (policy.allow_file_ops ? "true" : "false") << ",";
    json << "\"allow_system_changes\":" << (policy.allow_system_changes ? "true" : "false");
    json << "}";
    return json.str();
}

void ExecutionMemoryStore::Record(const std::string& nodeId, const ActionExecutionResult& result) {
    if (nodeId.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_memoryMutex);
    SuccessStats& stats = g_memory.stats[nodeId];

    if (result.status == "success") {
        ++stats.successCount;
    } else {
        ++stats.failureCount;
    }

    if (result.usedFallback) {
        ++stats.fallbackUsageCount;
    }

    const std::uint64_t samples = stats.successCount + stats.failureCount;
    const double latency = result.executionDurationMs > 0 ? static_cast<double>(result.executionDurationMs) : 0.0;
    if (samples == 1U) {
        stats.averageLatencyMs = latency;
    } else {
        stats.averageLatencyMs =
            ((stats.averageLatencyMs * static_cast<double>(samples - 1U)) + latency) / static_cast<double>(samples);
    }
}

double ExecutionMemoryStore::SuccessBias(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(g_memoryMutex);
    const auto it = g_memory.stats.find(nodeId);
    if (it == g_memory.stats.end()) {
        return 0.0;
    }

    const std::uint64_t samples = it->second.successCount + it->second.failureCount;
    if (samples == 0U) {
        return 0.0;
    }

    const double successRate = static_cast<double>(it->second.successCount) / static_cast<double>(samples);
    const double fallbackRate = static_cast<double>(it->second.fallbackUsageCount) / static_cast<double>(samples);
    return std::clamp((0.75 * successRate) + (0.25 * (1.0 - fallbackRate)), 0.0, 1.0);
}

std::string ExecutionMemoryStore::SerializeJson(std::size_t limit) {
    std::vector<std::pair<std::string, SuccessStats>> entries;

    {
        std::lock_guard<std::mutex> lock(g_memoryMutex);
        entries.reserve(g_memory.stats.size());
        for (const auto& entry : g_memory.stats) {
            entries.push_back(entry);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        const std::uint64_t leftSamples = left.second.successCount + left.second.failureCount;
        const std::uint64_t rightSamples = right.second.successCount + right.second.failureCount;

        const double leftRate = leftSamples == 0U ? 0.0 :
            static_cast<double>(left.second.successCount) / static_cast<double>(leftSamples);
        const double rightRate = rightSamples == 0U ? 0.0 :
            static_cast<double>(right.second.successCount) / static_cast<double>(rightSamples);

        if (std::abs(leftRate - rightRate) > 0.0001) {
            return leftRate > rightRate;
        }
        return left.first < right.first;
    });

    if (entries.size() > limit) {
        entries.resize(limit);
    }

    std::ostringstream json;
    json << "{";
    json << "\"nodes\":[";

    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            json << ",";
        }

        const std::uint64_t samples = entries[i].second.successCount + entries[i].second.failureCount;
        const double successRate = samples == 0U ? 0.0 :
            static_cast<double>(entries[i].second.successCount) / static_cast<double>(samples);

        json << "{";
        json << "\"node_id\":\"" << EscapeJson(entries[i].first) << "\",";
        json << "\"success_count\":" << entries[i].second.successCount << ",";
        json << "\"failure_count\":" << entries[i].second.failureCount << ",";
        json << "\"fallback_usage_count\":" << entries[i].second.fallbackUsageCount << ",";
        json << "\"average_latency_ms\":" << entries[i].second.averageLatencyMs << ",";
        json << "\"success_rate\":" << successRate;
        json << "}";
    }

    json << "]}";
    return json.str();
}

void TemporalStateEngine::Record(const EnvironmentState& state) {
    if (!state.unifiedState.valid) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    StateTransitionInfo transition;
    if (!history_.empty()) {
        const UnifiedState& previous = history_.back();
        transition.fromFrame = previous.frameId;
        transition.toFrame = state.unifiedState.frameId;
        transition.changed = previous.signature != state.unifiedState.signature;
        transition.uiChanged = previous.interactionGraph.signature != state.unifiedState.interactionGraph.signature;
        transition.elapsedMs =
            EpochMs(state.unifiedState.capturedAt) - EpochMs(previous.capturedAt);
        transition.stable = !transition.changed && transition.elapsedMs >= 30;
        transitions_.push_back(transition);
        while (transitions_.size() > 255U) {
            transitions_.pop_front();
        }
    }

    history_.push_back(state.unifiedState);
    while (history_.size() > 256U) {
        history_.pop_front();
    }
}

StateHistory TemporalStateEngine::Snapshot(std::size_t limit) const {
    StateHistory snapshot;

    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t start = history_.size() > limit ? history_.size() - limit : 0U;
    for (std::size_t index = start; index < history_.size(); ++index) {
        snapshot.history.push_back(history_[index]);
    }

    return snapshot;
}

StateTransitionInfo TemporalStateEngine::LatestTransition() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (transitions_.empty()) {
        return {};
    }
    return transitions_.back();
}

bool TemporalStateEngine::IsStable(std::size_t minStableSamples) const {
    const std::size_t boundedSamples = std::max<std::size_t>(2U, minStableSamples);

    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.size() < boundedSamples) {
        return false;
    }

    const UnifiedState& newest = history_.back();
    for (std::size_t i = 1; i < boundedSamples; ++i) {
        if (history_[history_.size() - 1U - i].signature != newest.signature) {
            return false;
        }
    }

    return true;
}

FrameConsistencyMetrics TemporalStateEngine::FrameConsistency(std::size_t limit) const {
    FrameConsistencyMetrics metrics;

    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.empty()) {
        return metrics;
    }

    const std::size_t start = history_.size() > limit ? history_.size() - limit : 0U;
    const UnifiedState& first = history_[start];
    const UnifiedState& last = history_.back();

    if (last.frameId >= first.frameId) {
        metrics.expectedFrames = (last.frameId - first.frameId) + 1U;
    }

    metrics.actualFrames = history_.size() - start;
    if (metrics.expectedFrames > metrics.actualFrames) {
        metrics.skippedFrames = metrics.expectedFrames - metrics.actualFrames;
    }

    if (metrics.expectedFrames > 0U) {
        metrics.score = static_cast<double>(metrics.actualFrames) / static_cast<double>(metrics.expectedFrames);
    }

    return metrics;
}

std::string TemporalStateEngine::SerializeJson(std::size_t limit) const {
    const StateHistory snapshot = Snapshot(limit);
    const StateTransitionInfo transition = LatestTransition();
    const FrameConsistencyMetrics consistency = FrameConsistency(limit);

    std::ostringstream json;
    json << "{";
    json << "\"count\":" << snapshot.history.size() << ",";
    json << "\"stable\":" << (IsStable(2U) ? "true" : "false") << ",";
    json << "\"latest_transition\":{";
    json << "\"changed\":" << (transition.changed ? "true" : "false") << ",";
    json << "\"ui_changed\":" << (transition.uiChanged ? "true" : "false") << ",";
    json << "\"stable\":" << (transition.stable ? "true" : "false") << ",";
    json << "\"from_frame\":" << transition.fromFrame << ",";
    json << "\"to_frame\":" << transition.toFrame << ",";
    json << "\"elapsed_ms\":" << transition.elapsedMs;
    json << "},";
    json << "\"frame_consistency\":{";
    json << "\"expected_frames\":" << consistency.expectedFrames << ",";
    json << "\"actual_frames\":" << consistency.actualFrames << ",";
    json << "\"skipped_frames\":" << consistency.skippedFrames << ",";
    json << "\"score\":" << consistency.score;
    json << "},";
    json << "\"history\":[";

    for (std::size_t i = 0; i < snapshot.history.size(); ++i) {
        if (i > 0) {
            json << ",";
        }

        const UnifiedState& entry = snapshot.history[i];
        json << "{";
        json << "\"frame_id\":" << entry.frameId << ",";
        json << "\"environment_sequence\":" << entry.environmentSequence << ",";
        json << "\"signature\":" << entry.signature << ",";
        json << "\"captured_at_ms\":" << EpochMs(entry.capturedAt) << ",";
        json << "\"graph_version\":" << entry.interactionGraph.version << ",";
        json << "\"graph_signature\":" << entry.interactionGraph.signature;
        json << "}";
    }

    json << "]}";
    return json.str();
}

IntentSequenceExecutor::IntentSequenceExecutor(
    IntentRegistry& registry,
    ExecutionEngine& executionEngine,
    Telemetry& telemetry)
    : registry_(registry),
      executionEngine_(executionEngine),
      telemetry_(telemetry) {}

IntentSequenceExecutionResult IntentSequenceExecutor::Execute(const IntentSequence& sequence, bool stopOnFailure) {
    IntentSequenceExecutionResult output;
    output.traceId = telemetry_.NewTraceId();

    if (sequence.steps.empty()) {
        output.reason = "sequence_empty";
        return output;
    }

    ActionExecutor executor(registry_, executionEngine_, telemetry_);

    for (std::size_t index = 0; index < sequence.steps.size(); ++index) {
        const ActionExecutionResult stepResult = executor.Act(sequence.steps[index]);
        output.attemptedSteps = index + 1U;

        IntentSequenceStepTrace trace;
        trace.index = index;
        trace.result = stepResult;
        output.stepTraces.push_back(std::move(trace));

        if (stepResult.status == "success") {
            ++output.completedSteps;
            continue;
        }

        output.failedStep = static_cast<int>(index);
        output.reason = stepResult.reason.empty() ? "sequence_step_failed" : stepResult.reason;
        if (stopOnFailure) {
            break;
        }
    }

    if (output.completedSteps == sequence.steps.size()) {
        output.status = "success";
        output.failedStep = -1;
        output.reason.clear();
    }

    return output;
}

WorkflowExecutor::WorkflowExecutor(IntentRegistry& registry, ExecutionEngine& executionEngine, Telemetry& telemetry)
    : executor_(registry, executionEngine, telemetry) {}

IntentSequenceExecutionResult WorkflowExecutor::runWorkflow(IntentSequence& sequence) {
    IntentSequenceExecutionResult result = executor_.Execute(sequence, true);
    if (result.status != "success" && result.reason.empty()) {
        result.reason = "workflow_failed";
    }
    return result;
}

bool SemanticPlannerBridge::ParseSemanticTaskRequestJson(
    std::string_view payload,
    SemanticTaskRequest* request,
    std::string* error) {
    if (request == nullptr) {
        if (error != nullptr) {
            *error = "request_out_null";
        }
        return false;
    }

    SemanticTaskRequest parsed;
    if (!ExtractJsonStringField(payload, "goal", &parsed.goal) || parsed.goal.empty()) {
        if (error != nullptr) {
            *error = "missing_goal";
        }
        return false;
    }

    std::string contextObject;
    if (ExtractJsonObjectField(payload, "context", &contextObject)) {
        std::string app;
        std::string domain;

        if (ExtractJsonStringField(contextObject, "app", &app)) {
            parsed.context.app = app;
        }
        if (ExtractJsonStringField(contextObject, "domain", &domain)) {
            parsed.context.domain = domain;
        }
    }

    *request = std::move(parsed);
    return true;
}

SemanticPlanResult SemanticPlannerBridge::Plan(const SemanticTaskRequest& request) {
    SemanticPlanResult result;

    const std::string lowerGoal = ToAsciiLower(request.goal);
    result.taskRequest.goal = request.goal;
    result.taskRequest.targetHint = request.goal;
    result.taskRequest.allowHidden = true;
    result.taskRequest.maxPlans = 4;

    if (!request.context.domain.empty()) {
        result.taskRequest.domain = TaskPlanner::ParseDomain(request.context.domain);
    } else if (lowerGoal.find("browser") != std::string::npos ||
        lowerGoal.find("search") != std::string::npos ||
        lowerGoal.find("url") != std::string::npos) {
        result.taskRequest.domain = TaskDomain::Browser;
    } else if (
        lowerGoal.find("slide") != std::string::npos ||
        lowerGoal.find("presentation") != std::string::npos ||
        lowerGoal.find("export") != std::string::npos) {
        result.taskRequest.domain = TaskDomain::Presentation;
    } else {
        result.taskRequest.domain = TaskDomain::Generic;
    }

    const bool externalRequested = GetEnvironmentVariableA("IEE_SEMANTIC_MODE", nullptr, 0) > 0;

    if (lowerGoal.find(" then ") != std::string::npos) {
        result.mode = "intent_sequence";
        result.sequenceGenerated = true;

        std::vector<std::string> clauses = SplitPhrase(request.goal, " then ");
        for (const std::string& clause : clauses) {
            ActionRequest step;
            if (!ParseActLikePhrase(clause, &step)) {
                step.action = "activate";
                step.target = clause;
            }
            step.context = request.context;
            result.intentSequence.steps.push_back(std::move(step));
        }

        result.diagnostics = externalRequested
            ? "external_semantic_requested_but_unconfigured_using_deterministic_sequence"
            : "deterministic_sequence_generated";
        return result;
    }

    result.mode = "task_request";
    result.sequenceGenerated = false;
    result.diagnostics = externalRequested
        ? "external_semantic_requested_but_unconfigured_using_deterministic_task_request"
        : "deterministic_task_request_generated";

    return result;
}

std::string SemanticPlannerBridge::SerializePlanJson(const SemanticPlanResult& plan) {
    std::ostringstream json;
    json << "{";
    json << "\"mode\":\"" << EscapeJson(plan.mode) << "\",";
    json << "\"sequence_generated\":" << (plan.sequenceGenerated ? "true" : "false") << ",";
    json << "\"diagnostics\":\"" << EscapeJson(plan.diagnostics) << "\",";
    json << "\"task_request\":{";
    json << "\"goal\":\"" << EscapeJson(plan.taskRequest.goal) << "\",";
    json << "\"target_hint\":\"" << EscapeJson(plan.taskRequest.targetHint) << "\",";
    json << "\"domain\":\"" << EscapeJson(TaskPlanner::ToString(plan.taskRequest.domain)) << "\",";
    json << "\"allow_hidden\":" << (plan.taskRequest.allowHidden ? "true" : "false") << ",";
    json << "\"max_plans\":" << plan.taskRequest.maxPlans;
    json << "},";
    json << "\"intent_sequence\":{";
    json << "\"steps\":[";

    for (std::size_t i = 0; i < plan.intentSequence.steps.size(); ++i) {
        if (i > 0) {
            json << ",";
        }

        const ActionRequest& step = plan.intentSequence.steps[i];
        json << "{";
        json << "\"action\":\"" << EscapeJson(step.action) << "\",";
        json << "\"target\":\"" << EscapeJson(step.target) << "\",";
        json << "\"value\":\"" << EscapeJson(step.value) << "\",";
        json << "\"context\":{";
        json << "\"app\":\"" << EscapeJson(step.context.app) << "\",";
        json << "\"domain\":\"" << EscapeJson(step.context.domain) << "\"";
        json << "}";
        json << "}";
    }

    json << "]";
    json << "}";
    json << "}";
    return json.str();
}

bool ParseIntentSequenceJson(std::string_view payload, IntentSequence* sequence, std::string* error) {
    if (sequence == nullptr) {
        if (error != nullptr) {
            *error = "sequence_out_null";
        }
        return false;
    }

    std::string stepsArray;
    if (!ExtractJsonArrayField(payload, "steps", &stepsArray)) {
        if (error != nullptr) {
            *error = "missing_steps";
        }
        return false;
    }

    const std::vector<std::string> stepObjects = ParseObjectArrayItems(stepsArray);
    if (stepObjects.empty()) {
        if (error != nullptr) {
            *error = "steps_empty";
        }
        return false;
    }

    IntentSequence parsed;
    parsed.steps.reserve(stepObjects.size());

    for (const std::string& object : stepObjects) {
        ActionRequest request;
        std::string parseError;
        if (!ParseActionRequestJson(object, &request, &parseError)) {
            if (error != nullptr) {
                *error = parseError.empty() ? "invalid_sequence_step" : parseError;
            }
            return false;
        }
        parsed.steps.push_back(std::move(request));
    }

    *sequence = std::move(parsed);
    return true;
}

std::string SerializeIntentSequenceExecutionResultJson(const IntentSequenceExecutionResult& result) {
    std::ostringstream json;
    json << "{";
    json << "\"status\":\"" << EscapeJson(result.status) << "\",";
    json << "\"trace_id\":\"" << EscapeJson(result.traceId) << "\",";
    json << "\"attempted_steps\":" << result.attemptedSteps << ",";
    json << "\"completed_steps\":" << result.completedSteps << ",";
    json << "\"failed_step\":" << result.failedStep;

    if (!result.reason.empty()) {
        json << ",\"reason\":\"" << EscapeJson(result.reason) << "\"";
    }

    json << ",\"steps\":[";
    for (std::size_t i = 0; i < result.stepTraces.size(); ++i) {
        if (i > 0) {
            json << ",";
        }
        json << "{";
        json << "\"index\":" << result.stepTraces[i].index << ",";
        json << "\"result\":" << SerializeActionExecutionResultJson(result.stepTraces[i].result);
        json << "}";
    }
    json << "]";

    json << "}";
    return json.str();
}

std::string SerializeUcpActEnvelope(const ActionExecutionResult& result) {
    std::ostringstream json;
    json << "{";
    json << "\"ucp_version\":\"1.0\",";
    json << "\"operation\":\"act\",";
    json << "\"result\":" << SerializeActionExecutionResultJson(result);
    json << "}";
    return json.str();
}

std::string SerializeUcpStateEnvelope(const AIStateView& stateView) {
    std::ostringstream json;
    json << "{";
    json << "\"ucp_version\":\"1.0\",";
    json << "\"operation\":\"state\",";
    json << "\"state\":" << AIStateViewProjector::SerializeJson(stateView);
    json << "}";
    return json.str();
}

}  // namespace iee
