#include "Telemetry.h"

#include <Windows.h>

#include <algorithm>
#include <iomanip>
#include <objbase.h>
#include <sstream>

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

std::int64_t EpochMs(std::chrono::system_clock::time_point timePoint) {
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count());
}

std::string GuidString() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return "trace-fallback";
    }

    wchar_t wideBuffer[64] = {};
    const int written = StringFromGUID2(guid, wideBuffer, 64);
    if (written <= 1) {
        return "trace-fallback";
    }

    std::wstring value(wideBuffer);
    if (!value.empty() && value.front() == L'{') {
        value.erase(value.begin());
    }
    if (!value.empty() && value.back() == L'}') {
        value.pop_back();
    }
    return Narrow(value);
}

}  // namespace

Telemetry::Telemetry()
    : startedAt_(std::chrono::steady_clock::now()) {}

std::string Telemetry::NewTraceId() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++nextTraceCounter_;

    std::ostringstream stream;
    stream << GuidString() << "-" << nextTraceCounter_;
    return stream.str();
}

void Telemetry::LogExecution(const ExecutionTrace& trace) {
    std::lock_guard<std::mutex> lock(mutex_);

    traces_.push_back(trace);
    if (traces_.size() > kMaxTraces) {
        traces_.pop_front();
    }

    ++totalExecutions_;
    if (trace.status == "SUCCESS" || trace.status == "PARTIAL") {
        ++successCount_;
    } else {
        ++failureCount_;
    }

    totalDurationMs_ += static_cast<double>(trace.durationMs);

    AdapterAggregate& aggregate = adapterAggregates_[trace.adapter];
    ++aggregate.executions;
    if (trace.status == "SUCCESS" || trace.status == "PARTIAL") {
        ++aggregate.successes;
    }
    aggregate.totalLatencyMs += static_cast<double>(trace.durationMs);
}

void Telemetry::LogAdapterDecision(
    const std::string& traceId,
    IntentAction action,
    const std::wstring& target,
    const std::string& selectedAdapter,
    const std::vector<std::pair<std::string, float>>& rankedScores,
    bool fastPath) {
    std::lock_guard<std::mutex> lock(mutex_);

    AdapterDecisionEvent event;
    event.traceId = traceId;
    event.action = ToString(action);
    event.target = target;
    event.selectedAdapter = selectedAdapter;
    event.rankedScores = rankedScores;
    event.fastPath = fastPath;
    event.timestamp = std::chrono::system_clock::now();

    adapterDecisions_.push_back(std::move(event));
    if (adapterDecisions_.size() > kMaxDecisions) {
        adapterDecisions_.pop_front();
    }
}

void Telemetry::LogFailure(const std::string& traceId, const std::string& component, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream stream;
    stream << "[" << traceId << "]" << "[" << component << "] " << message;
    failures_.push_back(stream.str());

    if (failures_.size() > kMaxFailures) {
        failures_.pop_front();
    }
}

void Telemetry::LogResolutionTiming(std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++resolutionSamples_;
    totalResolutionMs_ += static_cast<double>(duration.count());
}

std::vector<ExecutionTrace> Telemetry::RecentExecutions(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::size_t start = traces_.size() > limit ? traces_.size() - limit : 0;
    std::vector<ExecutionTrace> result;
    result.reserve(traces_.size() - start);

    for (std::size_t index = start; index < traces_.size(); ++index) {
        result.push_back(traces_[index]);
    }

    return result;
}

std::optional<ExecutionTrace> Telemetry::FindTrace(const std::string& traceId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(traces_.begin(), traces_.end(), [&](const ExecutionTrace& trace) {
        return trace.traceId == traceId;
    });

    if (it == traces_.end()) {
        return std::nullopt;
    }

    return *it;
}

TelemetrySnapshot Telemetry::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    TelemetrySnapshot snapshot;
    snapshot.totalExecutions = totalExecutions_;
    snapshot.successCount = successCount_;
    snapshot.failureCount = failureCount_;

    if (totalExecutions_ > 0) {
        snapshot.successRate = static_cast<double>(successCount_) / static_cast<double>(totalExecutions_);
        snapshot.averageLatencyMs = totalDurationMs_ / static_cast<double>(totalExecutions_);
    }

    if (resolutionSamples_ > 0) {
        snapshot.averageResolutionMs = totalResolutionMs_ / static_cast<double>(resolutionSamples_);
    }

    snapshot.uptimeMs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt_)
            .count());

    snapshot.adapterMetrics.reserve(adapterAggregates_.size());
    for (const auto& entry : adapterAggregates_) {
        AdapterTelemetryMetrics metrics;
        metrics.adapter = entry.first;
        metrics.executions = entry.second.executions;
        metrics.successes = entry.second.successes;

        if (entry.second.executions > 0) {
            metrics.successRate = static_cast<double>(entry.second.successes) / static_cast<double>(entry.second.executions);
            metrics.averageLatencyMs = entry.second.totalLatencyMs / static_cast<double>(entry.second.executions);
        }

        snapshot.adapterMetrics.push_back(std::move(metrics));
    }

    std::sort(
        snapshot.adapterMetrics.begin(),
        snapshot.adapterMetrics.end(),
        [](const AdapterTelemetryMetrics& left, const AdapterTelemetryMetrics& right) {
            return left.adapter < right.adapter;
        });

    return snapshot;
}

std::string Telemetry::SerializeSnapshotJson() const {
    const TelemetrySnapshot snapshot = Snapshot();

    std::ostringstream stream;
    stream << "{";
    stream << "\"totalExecutions\":" << snapshot.totalExecutions << ",";
    stream << "\"successCount\":" << snapshot.successCount << ",";
    stream << "\"failureCount\":" << snapshot.failureCount << ",";
    stream << "\"successRate\":" << std::fixed << std::setprecision(4) << snapshot.successRate << ",";
    stream << "\"averageLatencyMs\":" << std::fixed << std::setprecision(2) << snapshot.averageLatencyMs << ",";
    stream << "\"averageResolutionMs\":" << std::fixed << std::setprecision(2) << snapshot.averageResolutionMs << ",";
    stream << "\"uptimeMs\":" << snapshot.uptimeMs << ",";
    stream << "\"adapters\":[";

    for (std::size_t index = 0; index < snapshot.adapterMetrics.size(); ++index) {
        const AdapterTelemetryMetrics& metrics = snapshot.adapterMetrics[index];
        if (index > 0) {
            stream << ",";
        }

        stream << "{";
        stream << "\"adapter\":\"" << EscapeJson(metrics.adapter) << "\",";
        stream << "\"executions\":" << metrics.executions << ",";
        stream << "\"successes\":" << metrics.successes << ",";
        stream << "\"successRate\":" << std::fixed << std::setprecision(4) << metrics.successRate << ",";
        stream << "\"averageLatencyMs\":" << std::fixed << std::setprecision(2) << metrics.averageLatencyMs;
        stream << "}";
    }

    stream << "]";
    stream << "}";
    return stream.str();
}

std::string Telemetry::SerializeTraceJson(const std::string& traceId) const {
    const auto trace = FindTrace(traceId);
    if (!trace.has_value()) {
        return "{\"error\":\"trace_not_found\"}";
    }

    std::ostringstream stream;
    stream << "{";
    stream << "\"trace_id\":\"" << EscapeJson(trace->traceId) << "\",";
    stream << "\"intent\":\"" << EscapeJson(trace->intent) << "\",";
    stream << "\"target\":\"" << EscapeJson(Narrow(trace->target)) << "\",";
    stream << "\"adapter\":\"" << EscapeJson(trace->adapter) << "\",";
    stream << "\"duration_ms\":" << trace->durationMs << ",";
    stream << "\"status\":\"" << EscapeJson(trace->status) << "\",";
    stream << "\"message\":\"" << EscapeJson(trace->message) << "\",";
    stream << "\"verified\":" << (trace->verified ? "true" : "false") << ",";
    stream << "\"attempts\":" << trace->attempts << ",";
    stream << "\"timestamp_ms\":" << EpochMs(trace->timestamp);
    stream << "}";
    return stream.str();
}

}  // namespace iee
