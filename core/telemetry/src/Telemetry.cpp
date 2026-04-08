#include "Telemetry.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
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

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
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

std::string SerializeTraceLine(const ExecutionTrace& trace) {
    std::ostringstream stream;
    stream << "{";
    stream << "\"trace_id\":\"" << EscapeJson(trace.traceId) << "\",";
    stream << "\"intent\":\"" << EscapeJson(trace.intent) << "\",";
    stream << "\"target\":\"" << EscapeJson(Narrow(trace.target)) << "\",";
    stream << "\"adapter\":\"" << EscapeJson(trace.adapter) << "\",";
    stream << "\"duration_ms\":" << trace.durationMs << ",";
    stream << "\"status\":\"" << EscapeJson(trace.status) << "\",";
    stream << "\"message\":\"" << EscapeJson(trace.message) << "\",";
    stream << "\"verified\":" << (trace.verified ? "true" : "false") << ",";
    stream << "\"attempts\":" << trace.attempts << ",";
    stream << "\"snapshot_version\":" << trace.snapshotVersion << ",";
    stream << "\"control_frame\":" << trace.controlFrame << ",";
    stream << "\"timestamp_ms\":" << EpochMs(trace.timestamp);
    stream << "}";
    return stream.str();
}

LatencyComponentStats BuildLatencyStats(std::vector<double> values) {
    LatencyComponentStats stats;
    if (values.empty()) {
        return stats;
    }

    double sum = 0.0;
    for (double value : values) {
        sum += value;
        if (value > stats.maxMs) {
            stats.maxMs = value;
        }
    }
    stats.averageMs = sum / static_cast<double>(values.size());

    std::sort(values.begin(), values.end());
    const std::size_t index = ((values.size() - 1U) * 95U) / 100U;
    stats.p95Ms = values[index];
    return stats;
}

VisionComponentStats BuildVisionStats(std::vector<double> values) {
    VisionComponentStats stats;
    if (values.empty()) {
        return stats;
    }

    double sum = 0.0;
    for (double value : values) {
        sum += value;
        if (value > stats.maxMs) {
            stats.maxMs = value;
        }
    }
    stats.averageMs = sum / static_cast<double>(values.size());

    std::sort(values.begin(), values.end());
    const std::size_t index = ((values.size() - 1U) * 95U) / 100U;
    stats.p95Ms = values[index];
    return stats;
}

double PercentileValue(const std::vector<double>& sortedValues, std::size_t percentile) {
    if (sortedValues.empty()) {
        return 0.0;
    }

    const std::size_t index = ((sortedValues.size() - 1U) * percentile) / 100U;
    return sortedValues[index];
}

double PercentileValuePermille(const std::vector<double>& sortedValues, std::size_t permille) {
    if (sortedValues.empty()) {
        return 0.0;
    }

    const std::size_t index = ((sortedValues.size() - 1U) * permille) / 1000U;
    return sortedValues[index];
}

}  // namespace

Telemetry::Telemetry()
    : startedAt_(std::chrono::steady_clock::now()) {
    std::error_code ec;
    persistenceDirectory_ = std::filesystem::current_path(ec) / "artifacts" / "telemetry";
    if (!ec) {
        std::filesystem::create_directories(persistenceDirectory_, ec);
    }

    persistenceEnabled_ = !ec;
    if (persistenceEnabled_) {
        StartPersistenceWorker();
    }
}

Telemetry::~Telemetry() {
    StopPersistenceWorker();
}

void Telemetry::StartPersistenceWorker() {
    std::lock_guard<std::mutex> lock(persistenceMutex_);
    if (!persistenceEnabled_ || persistenceThread_.joinable()) {
        return;
    }

    persistenceStopRequested_ = false;
    persistenceThread_ = std::thread(&Telemetry::PersistenceLoop, this);
}

void Telemetry::StopPersistenceWorker() {
    {
        std::lock_guard<std::mutex> lock(persistenceMutex_);
        persistenceStopRequested_ = true;
        persistenceCv_.notify_all();
    }

    if (persistenceThread_.joinable()) {
        persistenceThread_.join();
    }
}

std::string Telemetry::NewTraceId() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++nextTraceCounter_;

    std::ostringstream stream;
    stream << GuidString() << "-" << nextTraceCounter_;
    return stream.str();
}

void Telemetry::LogExecution(const ExecutionTrace& trace) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        traces_.push_back(trace);
        if (traces_.size() > kMaxTraces) {
            traces_.pop_front();
            traceBufferWrapped_ = true;
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

    EnqueuePersistence(trace);
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

void Telemetry::LogLatencyBreakdown(const LatencyBreakdownSample& sample) {
    std::lock_guard<std::mutex> lock(mutex_);

    latencyBreakdowns_.push_back(sample);
    if (latencyBreakdowns_.size() > kMaxLatencySamples) {
        latencyBreakdowns_.pop_front();
    }
}

void Telemetry::LogVisionSample(const VisionLatencySample& sample) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (sample.frameId > 0 && latestVisionFrameId_ > 0 && sample.frameId > latestVisionFrameId_ + 1U) {
        visionDroppedFrames_ += (sample.frameId - latestVisionFrameId_ - 1U);
    }
    if (sample.frameId > latestVisionFrameId_) {
        latestVisionFrameId_ = sample.frameId;
    }

    visionSamples_.push_back(sample);
    if (visionSamples_.size() > kMaxVisionSamples) {
        visionSamples_.pop_front();
    }

    if (sample.simulated) {
        ++visionSimulatedSamples_;
    }
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

std::vector<ExecutionTrace> Telemetry::QueryExecutions(
    std::size_t limit,
    const std::string& statusFilter,
    const std::string& adapterFilter) const {
    const std::string normalizedStatus = LowerAscii(statusFilter);
    const std::string normalizedAdapter = LowerAscii(adapterFilter);

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ExecutionTrace> result;
    result.reserve(limit);

    for (auto it = traces_.rbegin(); it != traces_.rend(); ++it) {
        if (!normalizedStatus.empty() && LowerAscii(it->status) != normalizedStatus) {
            continue;
        }

        if (!normalizedAdapter.empty() && LowerAscii(it->adapter) != normalizedAdapter) {
            continue;
        }

        result.push_back(*it);
        if (result.size() >= limit) {
            break;
        }
    }

    std::reverse(result.begin(), result.end());
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
    TelemetrySnapshot snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        snapshot.totalExecutions = totalExecutions_;
        snapshot.successCount = successCount_;
        snapshot.failureCount = failureCount_;
        snapshot.traceBufferSize = traces_.size();
        snapshot.traceBufferWrapped = traceBufferWrapped_;

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
    }

    {
        std::lock_guard<std::mutex> lock(persistenceMutex_);
        snapshot.persistedTraceCount = persistedTraceCount_;
        snapshot.droppedPersistenceCount = droppedPersistenceCount_;
        snapshot.rotationFileIndex = currentFileIndex_;
    }

    std::sort(
        snapshot.adapterMetrics.begin(),
        snapshot.adapterMetrics.end(),
        [](const AdapterTelemetryMetrics& left, const AdapterTelemetryMetrics& right) {
            return left.adapter < right.adapter;
        });

    return snapshot;
}

TelemetryPersistenceStatus Telemetry::PersistenceStatus() const {
    TelemetryPersistenceStatus status;
    std::lock_guard<std::mutex> lock(persistenceMutex_);
    status.enabled = persistenceEnabled_;
    status.queuedCount = persistenceQueue_.size();
    status.persistedCount = persistedTraceCount_;
    status.droppedCount = droppedPersistenceCount_;
    status.currentFileIndex = currentFileIndex_;
    status.files.assign(persistedFiles_.begin(), persistedFiles_.end());
    return status;
}

LatencyBreakdownSnapshot Telemetry::LatencySnapshot(std::size_t limit) const {
    LatencyBreakdownSnapshot snapshot;

    std::vector<double> observationValues;
    std::vector<double> perceptionValues;
    std::vector<double> queueValues;
    std::vector<double> executionValues;
    std::vector<double> verificationValues;
    std::vector<double> totalValues;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        const std::size_t cappedLimit = std::max<std::size_t>(1U, limit);
        const std::size_t start = latencyBreakdowns_.size() > cappedLimit ? latencyBreakdowns_.size() - cappedLimit : 0U;
        const std::size_t count = latencyBreakdowns_.size() - start;
        snapshot.sampleCount = count;

        observationValues.reserve(count);
        perceptionValues.reserve(count);
        queueValues.reserve(count);
        executionValues.reserve(count);
        verificationValues.reserve(count);
        totalValues.reserve(count);

        for (std::size_t index = start; index < latencyBreakdowns_.size(); ++index) {
            const LatencyBreakdownSample& sample = latencyBreakdowns_[index];
            observationValues.push_back(sample.observationMs);
            perceptionValues.push_back(sample.perceptionMs);
            queueValues.push_back(sample.queueWaitMs);
            executionValues.push_back(sample.executionMs);
            verificationValues.push_back(sample.verificationMs);
            totalValues.push_back(sample.totalMs);
        }

        if (!latencyBreakdowns_.empty()) {
            snapshot.latest = latencyBreakdowns_.back();
        }
    }

    snapshot.observation = BuildLatencyStats(std::move(observationValues));
    snapshot.perception = BuildLatencyStats(std::move(perceptionValues));
    snapshot.queueWait = BuildLatencyStats(std::move(queueValues));
    snapshot.execution = BuildLatencyStats(std::move(executionValues));
    snapshot.verification = BuildLatencyStats(std::move(verificationValues));
    snapshot.total = BuildLatencyStats(std::move(totalValues));
    return snapshot;
}

PerformanceContractSnapshot Telemetry::PerformanceContract(double targetBudgetMs, std::size_t limit) const {
    PerformanceContractSnapshot snapshot;
    snapshot.targetBudgetMs = std::max(1.0, targetBudgetMs);

    std::vector<double> totals;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const std::size_t cappedLimit = std::max<std::size_t>(1U, limit);
        const std::size_t start = latencyBreakdowns_.size() > cappedLimit ? latencyBreakdowns_.size() - cappedLimit : 0U;
        const std::size_t count = latencyBreakdowns_.size() - start;

        snapshot.sampleCount = count;
        totals.reserve(count);
        for (std::size_t index = start; index < latencyBreakdowns_.size(); ++index) {
            totals.push_back(latencyBreakdowns_[index].totalMs);
        }
    }

    if (totals.empty()) {
        return snapshot;
    }

    std::sort(totals.begin(), totals.end());
    snapshot.p50Ms = PercentileValue(totals, 50U);
    snapshot.p95Ms = PercentileValue(totals, 95U);
    snapshot.maxMs = totals.back();
    snapshot.jitterMs = std::max(0.0, snapshot.p95Ms - snapshot.p50Ms);

    double driftSum = 0.0;
    for (double totalMs : totals) {
        driftSum += std::abs(totalMs - snapshot.targetBudgetMs);
    }
    snapshot.driftMs = driftSum / static_cast<double>(totals.size());

    const double maxAllowance = snapshot.targetBudgetMs * 2.0;
    snapshot.withinBudget = snapshot.p95Ms <= snapshot.targetBudgetMs &&
        snapshot.maxMs <= maxAllowance &&
        snapshot.driftMs <= snapshot.targetBudgetMs;

    return snapshot;
}

LatencyPercentilesSnapshot Telemetry::LatencyPercentiles(std::size_t limit) const {
    LatencyPercentilesSnapshot snapshot;

    std::vector<double> totals;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const std::size_t cappedLimit = std::max<std::size_t>(1U, limit);
        const std::size_t start = latencyBreakdowns_.size() > cappedLimit ? latencyBreakdowns_.size() - cappedLimit : 0U;
        const std::size_t count = latencyBreakdowns_.size() - start;

        snapshot.sampleCount = count;
        totals.reserve(count);
        for (std::size_t index = start; index < latencyBreakdowns_.size(); ++index) {
            totals.push_back(latencyBreakdowns_[index].totalMs);
        }
    }

    if (totals.empty()) {
        return snapshot;
    }

    std::sort(totals.begin(), totals.end());
    snapshot.p50Ms = PercentileValue(totals, 50U);
    snapshot.p95Ms = PercentileValue(totals, 95U);
    snapshot.p99Ms = PercentileValue(totals, 99U);
    snapshot.p999Ms = PercentileValuePermille(totals, 999U);
    return snapshot;
}

VisionSnapshot Telemetry::VisionLatencySnapshot(std::size_t limit) const {
    VisionSnapshot snapshot;

    std::vector<double> captureValues;
    std::vector<double> detectionValues;
    std::vector<double> mergeValues;
    std::vector<double> totalValues;
    std::size_t simulatedCount = 0;
    std::chrono::system_clock::time_point firstTimestamp;
    std::chrono::system_clock::time_point lastTimestamp;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        const std::size_t cappedLimit = std::max<std::size_t>(1U, limit);
        const std::size_t start = visionSamples_.size() > cappedLimit ? visionSamples_.size() - cappedLimit : 0U;
        const std::size_t count = visionSamples_.size() - start;

        snapshot.sampleCount = count;
        snapshot.droppedFrames = visionDroppedFrames_;

        captureValues.reserve(count);
        detectionValues.reserve(count);
        mergeValues.reserve(count);
        totalValues.reserve(count);

        for (std::size_t index = start; index < visionSamples_.size(); ++index) {
            const VisionLatencySample& sample = visionSamples_[index];
            captureValues.push_back(sample.captureMs);
            detectionValues.push_back(sample.detectionMs);
            mergeValues.push_back(sample.mergeMs);
            totalValues.push_back(sample.totalMs);

            if (sample.simulated) {
                ++simulatedCount;
            }

            if (index == start) {
                firstTimestamp = sample.timestamp;
            }
            lastTimestamp = sample.timestamp;
        }

        if (!visionSamples_.empty()) {
            snapshot.latest = visionSamples_.back();
        }
    }

    snapshot.simulatedSamples = static_cast<std::uint64_t>(simulatedCount);
    snapshot.capture = BuildVisionStats(std::move(captureValues));
    snapshot.detection = BuildVisionStats(std::move(detectionValues));
    snapshot.merge = BuildVisionStats(std::move(mergeValues));
    snapshot.total = BuildVisionStats(std::move(totalValues));

    if (snapshot.sampleCount >= 2 && lastTimestamp > firstTimestamp) {
        const auto spanMs = std::chrono::duration_cast<std::chrono::milliseconds>(lastTimestamp - firstTimestamp).count();
        if (spanMs > 0) {
            snapshot.estimatedFps =
                (static_cast<double>(snapshot.sampleCount - 1U) * 1000.0) / static_cast<double>(spanMs);
        }
    }

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
    stream << "\"persistedTraceCount\":" << snapshot.persistedTraceCount << ",";
    stream << "\"droppedPersistenceCount\":" << snapshot.droppedPersistenceCount << ",";
    stream << "\"traceBufferSize\":" << snapshot.traceBufferSize << ",";
    stream << "\"traceBufferWrapped\":" << (snapshot.traceBufferWrapped ? "true" : "false") << ",";
    stream << "\"rotationFileIndex\":" << snapshot.rotationFileIndex << ",";
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

    return SerializeTraceLine(*trace);
}

std::string Telemetry::SerializePersistenceJson() const {
    const TelemetryPersistenceStatus status = PersistenceStatus();

    std::ostringstream stream;
    stream << "{";
    stream << "\"enabled\":" << (status.enabled ? "true" : "false") << ",";
    stream << "\"queuedCount\":" << status.queuedCount << ",";
    stream << "\"persistedCount\":" << status.persistedCount << ",";
    stream << "\"droppedCount\":" << status.droppedCount << ",";
    stream << "\"currentFileIndex\":" << status.currentFileIndex << ",";
    stream << "\"files\":[";

    for (std::size_t index = 0; index < status.files.size(); ++index) {
        if (index > 0) {
            stream << ",";
        }
        stream << "\"" << EscapeJson(status.files[index]) << "\"";
    }

    stream << "]";
    stream << "}";
    return stream.str();
}

std::string Telemetry::SerializeLatencyJson(std::size_t limit) const {
    const LatencyBreakdownSnapshot snapshot = LatencySnapshot(limit);

    const auto appendComponent = [](std::ostringstream* stream, const char* key, const LatencyComponentStats& stats, bool leadingComma) {
        if (leadingComma) {
            *stream << ",";
        }

        *stream << "\"" << key << "\":{";
        *stream << "\"avg_ms\":" << std::fixed << std::setprecision(3) << stats.averageMs << ",";
        *stream << "\"p95_ms\":" << std::fixed << std::setprecision(3) << stats.p95Ms << ",";
        *stream << "\"max_ms\":" << std::fixed << std::setprecision(3) << stats.maxMs;
        *stream << "}";
    };

    std::ostringstream stream;
    stream << "{";
    stream << "\"sample_count\":" << snapshot.sampleCount;

    appendComponent(&stream, "observation", snapshot.observation, true);
    appendComponent(&stream, "perception", snapshot.perception, true);
    appendComponent(&stream, "queue_wait", snapshot.queueWait, true);
    appendComponent(&stream, "execution", snapshot.execution, true);
    appendComponent(&stream, "verification", snapshot.verification, true);
    appendComponent(&stream, "total", snapshot.total, true);

    if (snapshot.latest.has_value()) {
        const LatencyBreakdownSample& latest = *snapshot.latest;
        stream << ",\"latest\":{";
        stream << "\"frame\":" << latest.frame << ",";
        stream << "\"trace_id\":\"" << EscapeJson(latest.traceId) << "\",";
        stream << "\"observation_ms\":" << std::fixed << std::setprecision(3) << latest.observationMs << ",";
        stream << "\"perception_ms\":" << std::fixed << std::setprecision(3) << latest.perceptionMs << ",";
        stream << "\"queue_wait_ms\":" << std::fixed << std::setprecision(3) << latest.queueWaitMs << ",";
        stream << "\"execution_ms\":" << std::fixed << std::setprecision(3) << latest.executionMs << ",";
        stream << "\"verification_ms\":" << std::fixed << std::setprecision(3) << latest.verificationMs << ",";
        stream << "\"total_ms\":" << std::fixed << std::setprecision(3) << latest.totalMs << ",";
        stream << "\"timestamp_ms\":" << EpochMs(latest.timestamp);
        stream << "}";
    }

    stream << "}";
    return stream.str();
}

std::string Telemetry::SerializePerformanceContractJson(double targetBudgetMs, std::size_t limit) const {
    const PerformanceContractSnapshot contract = PerformanceContract(targetBudgetMs, limit);

    std::ostringstream stream;
    stream << "{";
    stream << "\"sample_count\":" << contract.sampleCount << ",";
    stream << "\"target_budget_ms\":" << std::fixed << std::setprecision(3) << contract.targetBudgetMs << ",";
    stream << "\"p50_ms\":" << std::fixed << std::setprecision(3) << contract.p50Ms << ",";
    stream << "\"p95_ms\":" << std::fixed << std::setprecision(3) << contract.p95Ms << ",";
    stream << "\"max_ms\":" << std::fixed << std::setprecision(3) << contract.maxMs << ",";
    stream << "\"jitter_ms\":" << std::fixed << std::setprecision(3) << contract.jitterMs << ",";
    stream << "\"drift_ms\":" << std::fixed << std::setprecision(3) << contract.driftMs << ",";
    stream << "\"within_budget\":" << (contract.withinBudget ? "true" : "false");
    stream << "}";
    return stream.str();
}

std::string Telemetry::SerializeLatencyPercentilesJson(std::size_t limit) const {
    const LatencyPercentilesSnapshot snapshot = LatencyPercentiles(limit);

    std::ostringstream stream;
    stream << "{";
    stream << "\"sample_count\":" << snapshot.sampleCount << ",";
    stream << "\"p50_ms\":" << std::fixed << std::setprecision(3) << snapshot.p50Ms << ",";
    stream << "\"p95_ms\":" << std::fixed << std::setprecision(3) << snapshot.p95Ms << ",";
    stream << "\"p99_ms\":" << std::fixed << std::setprecision(3) << snapshot.p99Ms << ",";
    stream << "\"p999_ms\":" << std::fixed << std::setprecision(3) << snapshot.p999Ms;
    stream << "}";
    return stream.str();
}

std::string Telemetry::SerializeVisionJson(std::size_t limit) const {
    const VisionSnapshot snapshot = VisionLatencySnapshot(limit);

    std::ostringstream stream;
    stream << "{";
    stream << "\"sample_count\":" << snapshot.sampleCount << ",";
    stream << "\"simulated_samples\":" << snapshot.simulatedSamples << ",";
    stream << "\"dropped_frames\":" << snapshot.droppedFrames << ",";
    stream << "\"estimated_fps\":" << std::fixed << std::setprecision(3) << snapshot.estimatedFps;

    const auto appendComponent = [](std::ostringstream* out, const char* name, const VisionComponentStats& stats, bool leadingComma) {
        if (leadingComma) {
            *out << ",";
        }
        *out << "\"" << name << "\":{";
        *out << "\"avg_ms\":" << std::fixed << std::setprecision(3) << stats.averageMs << ",";
        *out << "\"p95_ms\":" << std::fixed << std::setprecision(3) << stats.p95Ms << ",";
        *out << "\"max_ms\":" << std::fixed << std::setprecision(3) << stats.maxMs;
        *out << "}";
    };

    appendComponent(&stream, "capture", snapshot.capture, true);
    appendComponent(&stream, "detection", snapshot.detection, true);
    appendComponent(&stream, "merge", snapshot.merge, true);
    appendComponent(&stream, "total", snapshot.total, true);

    if (snapshot.latest.has_value()) {
        const VisionLatencySample& latest = *snapshot.latest;
        stream << ",\"latest\":{";
        stream << "\"frame_id\":" << latest.frameId << ",";
        stream << "\"environment_sequence\":" << latest.environmentSequence << ",";
        stream << "\"capture_ms\":" << std::fixed << std::setprecision(3) << latest.captureMs << ",";
        stream << "\"detection_ms\":" << std::fixed << std::setprecision(3) << latest.detectionMs << ",";
        stream << "\"merge_ms\":" << std::fixed << std::setprecision(3) << latest.mergeMs << ",";
        stream << "\"total_ms\":" << std::fixed << std::setprecision(3) << latest.totalMs << ",";
        stream << "\"simulated\":" << (latest.simulated ? "true" : "false") << ",";
        stream << "\"timestamp_ms\":" << EpochMs(latest.timestamp);
        stream << "}";
    }

    stream << "}";
    return stream.str();
}

void Telemetry::EnqueuePersistence(const ExecutionTrace& trace) {
    if (!persistenceEnabled_) {
        return;
    }

    std::lock_guard<std::mutex> lock(persistenceMutex_);
    if (persistenceQueue_.size() >= kMaxPersistenceQueue) {
        ++droppedPersistenceCount_;
        return;
    }

    persistenceQueue_.push_back(trace);
    persistenceCv_.notify_one();
}

void Telemetry::PersistenceLoop() {
    while (true) {
        ExecutionTrace trace;

        {
            std::unique_lock<std::mutex> lock(persistenceMutex_);
            persistenceCv_.wait(lock, [this]() {
                return persistenceStopRequested_ || !persistenceQueue_.empty();
            });

            if (persistenceStopRequested_ && persistenceQueue_.empty()) {
                break;
            }

            trace = std::move(persistenceQueue_.front());
            persistenceQueue_.pop_front();
        }

        PersistTraceLine(trace);
    }
}

std::filesystem::path Telemetry::CurrentTraceFilePathLocked() const {
    std::ostringstream fileName;
    fileName << "trace_" << std::setw(3) << std::setfill('0') << currentFileIndex_ << ".jsonl";
    return persistenceDirectory_ / fileName.str();
}

void Telemetry::PersistTraceLine(const ExecutionTrace& trace) {
    const std::string line = SerializeTraceLine(trace);

    std::lock_guard<std::mutex> lock(persistenceMutex_);
    if (!persistenceEnabled_) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(persistenceDirectory_, ec);
    if (ec) {
        ++droppedPersistenceCount_;
        return;
    }

    const std::size_t writeBytes = line.size() + 1U;
    if (currentFileBytes_ > 0 && currentFileBytes_ + writeBytes > kMaxTraceFileBytes) {
        ++currentFileIndex_;
        currentFileBytes_ = 0;
    }

    const std::filesystem::path filePath = CurrentTraceFilePathLocked();
    const std::string fileName = filePath.filename().string();
    if (persistedFiles_.empty() || persistedFiles_.back() != fileName) {
        persistedFiles_.push_back(fileName);
    }

    while (persistedFiles_.size() > kMaxPersistedFiles) {
        const std::string oldFile = persistedFiles_.front();
        persistedFiles_.pop_front();
        std::filesystem::remove(persistenceDirectory_ / oldFile, ec);
        ec.clear();
    }

    std::ofstream output(filePath, std::ios::app | std::ios::binary);
    if (!output.is_open()) {
        ++droppedPersistenceCount_;
        return;
    }

    output << line << '\n';
    output.flush();

    currentFileBytes_ += writeBytes;
    ++persistedTraceCount_;
}

}  // namespace iee
