#include "ControlRuntime.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace iee {
namespace {

std::string BuildRuntimeId() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return "runtime-" + std::to_string(millis);
}

int ClampFrameMs(int value) {
    if (value < 1) {
        return 1;
    }
    if (value > 1000) {
        return 1000;
    }
    return value;
}

}  // namespace

ControlRuntime::ControlRuntime(
    IntentRegistry& registry,
    ExecutionEngine& executionEngine,
    EventBus& eventBus,
    Telemetry& telemetry)
    : registry_(registry),
      executionEngine_(executionEngine),
      eventBus_(eventBus),
      telemetry_(telemetry) {
    uiChangedSubscriptionId_ = eventBus_.Subscribe(EventType::UiChanged, [this](const Event& event) {
        HandleEvent(event);
    });

    fileChangedSubscriptionId_ = eventBus_.Subscribe(EventType::FileSystemChanged, [this](const Event& event) {
        HandleEvent(event);
    });

    errorSubscriptionId_ = eventBus_.Subscribe(EventType::Error, [this](const Event& event) {
        HandleEvent(event);
    });

    ambiguitySubscriptionId_ = eventBus_.Subscribe(EventType::AmbiguityDetected, [this](const Event& event) {
        HandleEvent(event);
    });
}

ControlRuntime::~ControlRuntime() {
    eventBus_.Unsubscribe(EventType::UiChanged, uiChangedSubscriptionId_);
    eventBus_.Unsubscribe(EventType::FileSystemChanged, fileChangedSubscriptionId_);
    eventBus_.Unsubscribe(EventType::Error, errorSubscriptionId_);
    eventBus_.Unsubscribe(EventType::AmbiguityDetected, ambiguitySubscriptionId_);

    Stop();
}

bool ControlRuntime::Start(const ControlRuntimeConfig& config, std::string* message) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        if (message != nullptr) {
            *message = "control runtime already running";
        }
        return false;
    }

    config_ = config;
    config_.targetFrameMs = ClampFrameMs(config_.targetFrameMs);

    runtimeId_ = BuildRuntimeId();
    nextSequence_ = 1;
    version_ = 0;
    framesExecuted_ = 0;
    intentsExecuted_ = 0;
    droppedFrames_ = 0;
    highPriorityEvents_ = 0;
    mediumPriorityEvents_ = 0;
    lowPriorityEvents_ = 0;
    pendingHighEvents_ = 0;
    pendingMediumEvents_ = 0;
    pendingLowEvents_ = 0;
    pendingUiRefresh_ = false;
    pendingFsRefresh_ = false;
    lastCycleMs_ = 0;
    latestSnapshotVersion_ = 0;
    lastTraceId_.clear();

    highQueue_.clear();
    mediumQueue_.clear();
    lowQueue_.clear();
    cycleLatenciesMs_.clear();

    stopRequested_ = false;
    running_ = true;

    worker_ = std::thread(&ControlRuntime::RunLoop, this);

    if (message != nullptr) {
        *message = "control runtime started";
    }

    return true;
}

ControlRuntimeSummary ControlRuntime::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
        running_ = false;
        wakeCv_.notify_all();
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto percentiles = ComputePercentilesLocked();

    ControlRuntimeSummary summary;
    summary.wasActive = !runtimeId_.empty();
    summary.runtimeId = runtimeId_;
    summary.framesExecuted = framesExecuted_;
    summary.intentsExecuted = intentsExecuted_;
    summary.droppedFrames = droppedFrames_;
    summary.p50LatencyMs = percentiles[0];
    summary.p95LatencyMs = percentiles[1];
    summary.p99LatencyMs = percentiles[2];
    return summary;
}

ControlRuntimeSnapshot ControlRuntime::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);

    ControlRuntimeSnapshot snapshot;
    snapshot.active = running_;
    snapshot.runtimeId = runtimeId_;
    snapshot.version = version_;
    snapshot.framesExecuted = framesExecuted_;
    snapshot.intentsExecuted = intentsExecuted_;
    snapshot.droppedFrames = droppedFrames_;
    snapshot.highPriorityEvents = highPriorityEvents_;
    snapshot.mediumPriorityEvents = mediumPriorityEvents_;
    snapshot.lowPriorityEvents = lowPriorityEvents_;
    snapshot.queuedHigh = highQueue_.size();
    snapshot.queuedMedium = mediumQueue_.size();
    snapshot.queuedLow = lowQueue_.size();
    snapshot.lastCycleMs = lastCycleMs_;
    snapshot.targetFrameMs = config_.targetFrameMs;
    snapshot.lastTraceId = lastTraceId_;
    snapshot.latestSnapshotVersion = latestSnapshotVersion_;
    return snapshot;
}

bool ControlRuntime::EnqueueIntent(const Intent& intent, ControlPriority priority) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_) {
        return false;
    }

    QueuedIntent queued;
    queued.intent = intent;
    queued.priority = priority;
    queued.sequence = nextSequence_++;

    switch (priority) {
    case ControlPriority::High:
        highQueue_.push_back(std::move(queued));
        break;
    case ControlPriority::Medium:
        mediumQueue_.push_back(std::move(queued));
        break;
    case ControlPriority::Low:
    default:
        lowQueue_.push_back(std::move(queued));
        break;
    }

    wakeCv_.notify_all();
    return true;
}

void ControlRuntime::HandleEvent(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_) {
        return;
    }

    switch (event.priority) {
    case EventPriority::HIGH:
        ++pendingHighEvents_;
        break;
    case EventPriority::MEDIUM:
        ++pendingMediumEvents_;
        break;
    case EventPriority::LOW:
    default:
        ++pendingLowEvents_;
        break;
    }

    if (event.type == EventType::UiChanged) {
        pendingUiRefresh_ = true;
    }
    if (event.type == EventType::FileSystemChanged) {
        pendingFsRefresh_ = true;
    }

    wakeCv_.notify_all();
}

bool ControlRuntime::PopIntentLocked(Intent* intent, ControlPriority* priority) {
    if (!highQueue_.empty()) {
        const QueuedIntent queued = std::move(highQueue_.front());
        highQueue_.pop_front();
        *intent = queued.intent;
        *priority = queued.priority;
        return true;
    }

    if (!mediumQueue_.empty()) {
        const QueuedIntent queued = std::move(mediumQueue_.front());
        mediumQueue_.pop_front();
        *intent = queued.intent;
        *priority = queued.priority;
        return true;
    }

    if (!lowQueue_.empty()) {
        const QueuedIntent queued = std::move(lowQueue_.front());
        lowQueue_.pop_front();
        *intent = queued.intent;
        *priority = queued.priority;
        return true;
    }

    return false;
}

std::array<double, 3> ControlRuntime::ComputePercentilesLocked() const {
    if (cycleLatenciesMs_.empty()) {
        return {0.0, 0.0, 0.0};
    }

    std::vector<std::int64_t> sorted = cycleLatenciesMs_;
    std::sort(sorted.begin(), sorted.end());

    const auto pick = [&sorted](std::size_t percentile) -> double {
        if (sorted.empty()) {
            return 0.0;
        }

        const std::size_t n = sorted.size();
        const std::size_t index = ((n - 1U) * percentile) / 100U;
        return static_cast<double>(sorted[index]);
    };

    return {pick(50U), pick(95U), pick(99U)};
}

void ControlRuntime::RunLoop() {
    const int frameMs = std::max(1, config_.targetFrameMs);
    auto nextFrame = std::chrono::steady_clock::now();

    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopRequested_) {
                break;
            }
            if (config_.maxFrames > 0 && framesExecuted_ >= config_.maxFrames) {
                stopRequested_ = true;
                running_ = false;
                break;
            }
        }

        const auto frameStart = std::chrono::steady_clock::now();
        std::uint64_t currentFrame = 0;

        bool refreshUi = false;
        bool refreshFs = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++version_;
            ++framesExecuted_;
            currentFrame = framesExecuted_;

            if (pendingHighEvents_ > 0) {
                --pendingHighEvents_;
                ++highPriorityEvents_;
            } else if (pendingMediumEvents_ > 0) {
                --pendingMediumEvents_;
                ++mediumPriorityEvents_;
            } else if (pendingLowEvents_ > 0) {
                --pendingLowEvents_;
                ++lowPriorityEvents_;
            }

            if (pendingUiRefresh_) {
                refreshUi = true;
                pendingUiRefresh_ = false;
            }

            if (pendingFsRefresh_) {
                refreshFs = true;
                pendingFsRefresh_ = false;
            }
        }

        if (refreshUi) {
            registry_.RefreshUiIncremental();
        }
        if (refreshFs) {
            registry_.RefreshFileSystemIncremental();
        }
        if (!refreshUi && !refreshFs && (currentFrame % 120U == 1U)) {
            registry_.Refresh();
        }

        const ObserverSnapshot snapshot = registry_.LastSnapshot();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            latestSnapshotVersion_ = snapshot.sequence;
        }

        Intent intent;
        ControlPriority priority{ControlPriority::Low};
        bool hasIntent = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hasIntent = PopIntentLocked(&intent, &priority);
        }

        if (hasIntent) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - frameStart)
                                     .count();
            const int remainingBudgetMs = frameMs - static_cast<int>(elapsed);

            if (remainingBudgetMs > 0) {
                intent.context.controlFrame = currentFrame;
                if (intent.context.snapshotVersion == 0) {
                    intent.context.snapshotVersion = snapshot.sequence;
                }
                if (intent.context.snapshotTicks == 0 && snapshot.sequence > 0) {
                    intent.context.snapshotTicks = snapshot.sequence;
                }

                const ExecutionResult result =
                    executionEngine_.ExecuteWithBudget(intent, std::chrono::milliseconds(remainingBudgetMs));
                if (result.IsSuccess() && !intent.id.empty()) {
                    registry_.RecordInteraction(intent.id);
                }

                std::lock_guard<std::mutex> lock(mutex_);
                ++intentsExecuted_;
                lastTraceId_ = result.traceId;
            } else {
                QueuedIntent queued;
                queued.intent = intent;
                queued.priority = priority;
                queued.sequence = 0;

                std::lock_guard<std::mutex> lock(mutex_);
                ++droppedFrames_;
                switch (priority) {
                case ControlPriority::High:
                    highQueue_.push_front(std::move(queued));
                    break;
                case ControlPriority::Medium:
                    mediumQueue_.push_front(std::move(queued));
                    break;
                case ControlPriority::Low:
                default:
                    lowQueue_.push_front(std::move(queued));
                    break;
                }
            }
        }

        const auto frameEnd = std::chrono::steady_clock::now();
        const std::int64_t cycleMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart).count();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastCycleMs_ = cycleMs;
            cycleLatenciesMs_.push_back(cycleMs);
            if (cycleLatenciesMs_.size() > kMaxLatencySamples) {
                const std::size_t removeCount = cycleLatenciesMs_.size() - kMaxLatencySamples;
                cycleLatenciesMs_.erase(cycleLatenciesMs_.begin(), cycleLatenciesMs_.begin() + removeCount);
            }

            if (cycleMs > frameMs) {
                ++droppedFrames_;
            }
        }

        nextFrame += std::chrono::milliseconds(frameMs);

        std::unique_lock<std::mutex> waitLock(mutex_);
        if (stopRequested_) {
            break;
        }

        wakeCv_.wait_until(waitLock, nextFrame, [this]() {
            return stopRequested_;
        });

        if (std::chrono::steady_clock::now() > nextFrame + std::chrono::milliseconds(frameMs)) {
            nextFrame = std::chrono::steady_clock::now();
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

std::string ControlRuntime::EscapeJson(const std::string& value) {
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

std::string ControlRuntime::ToString(ControlPriority priority) {
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

std::string ControlRuntime::SerializeSnapshotJson(const ControlRuntimeSnapshot& snapshot) {
    std::ostringstream stream;
    stream << "{";
    stream << "\"active\":" << (snapshot.active ? "true" : "false") << ",";
    stream << "\"runtime_id\":\"" << EscapeJson(snapshot.runtimeId) << "\",";
    stream << "\"version\":" << snapshot.version << ",";
    stream << "\"frames_executed\":" << snapshot.framesExecuted << ",";
    stream << "\"intents_executed\":" << snapshot.intentsExecuted << ",";
    stream << "\"dropped_frames\":" << snapshot.droppedFrames << ",";
    stream << "\"events\":{";
    stream << "\"high\":" << snapshot.highPriorityEvents << ",";
    stream << "\"medium\":" << snapshot.mediumPriorityEvents << ",";
    stream << "\"low\":" << snapshot.lowPriorityEvents;
    stream << "},";
    stream << "\"queue\":{";
    stream << "\"high\":" << snapshot.queuedHigh << ",";
    stream << "\"medium\":" << snapshot.queuedMedium << ",";
    stream << "\"low\":" << snapshot.queuedLow;
    stream << "},";
    stream << "\"last_cycle_ms\":" << snapshot.lastCycleMs << ",";
    stream << "\"target_frame_ms\":" << snapshot.targetFrameMs << ",";
    stream << "\"latest_snapshot_version\":" << snapshot.latestSnapshotVersion << ",";
    stream << "\"last_trace_id\":\"" << EscapeJson(snapshot.lastTraceId) << "\"";
    stream << "}";
    return stream.str();
}

std::string ControlRuntime::SerializeSummaryJson(const ControlRuntimeSummary& summary) {
    std::ostringstream stream;
    stream << "{";
    stream << "\"runtime_id\":\"" << EscapeJson(summary.runtimeId) << "\",";
    stream << "\"was_active\":" << (summary.wasActive ? "true" : "false") << ",";
    stream << "\"frames_executed\":" << summary.framesExecuted << ",";
    stream << "\"intents_executed\":" << summary.intentsExecuted << ",";
    stream << "\"dropped_frames\":" << summary.droppedFrames << ",";
    stream << "\"latency\":{";
    stream << "\"p50_ms\":" << summary.p50LatencyMs << ",";
    stream << "\"p95_ms\":" << summary.p95LatencyMs << ",";
    stream << "\"p99_ms\":" << summary.p99LatencyMs;
    stream << "}";
    stream << "}";
    return stream.str();
}

}  // namespace iee
