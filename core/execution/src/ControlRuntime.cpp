#include "ControlRuntime.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
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

int ClampDecisionBudgetMs(int value) {
    if (value < 1) {
        return 1;
    }
    if (value > 200) {
        return 200;
    }
    return value;
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

std::string ToAsciiLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch - 'A' + 'a');
        }
        return static_cast<char>(ch);
    });
    return value;
}

ControlPriority ParseControlPriorityHint(const Intent& intent) {
    const auto it = intent.params.values.find("control_priority");
    if (it == intent.params.values.end()) {
        return ControlPriority::Low;
    }

    const std::string value = ToAsciiLower(Narrow(it->second));
    if (value == "high") {
        return ControlPriority::High;
    }
    if (value == "medium") {
        return ControlPriority::Medium;
    }
    return ControlPriority::Low;
}

bool IsStateChangeExpected(IntentAction action) {
    switch (action) {
    case IntentAction::Activate:
    case IntentAction::SetValue:
    case IntentAction::Select:
    case IntentAction::Create:
    case IntentAction::Delete:
    case IntentAction::Move:
        return true;
    case IntentAction::Unknown:
    default:
        return false;
    }
}

bool IsLikelyMismatch(
    const Intent& intent,
    const FeedbackDelta& delta,
    const EnvironmentState& before,
    const EnvironmentState& after) {
    if (!IsStateChangeExpected(intent.action)) {
        return false;
    }

    const bool uiChanged = before.uiElements.size() != after.uiElements.size();
    const bool fileChanged = before.fileSystemEntries.size() != after.fileSystemEntries.size();
    const bool perceptionChanged = delta.signatureChanged ||
        std::abs(delta.focusRatioDelta) > 0.001 ||
        std::abs(delta.occupancyRatioDelta) > 0.001;

    return !uiChanged && !fileChanged && !perceptionChanged;
}

bool ShouldScheduleCorrection(const Intent& intent) {
    const auto retryMarkerIt = intent.params.values.find("feedback_retry");
    if (retryMarkerIt == intent.params.values.end()) {
        return true;
    }

    const std::wstring marker = retryMarkerIt->second;
    return !(marker == L"1" || marker == L"true" || marker == L"yes");
}

}  // namespace

ControlRuntime::ControlRuntime(
    IntentRegistry& registry,
    ExecutionEngine& executionEngine,
    EventBus& eventBus,
        Telemetry& telemetry,
        std::shared_ptr<EnvironmentAdapter> environmentAdapter)
    : registry_(registry),
      executionEngine_(executionEngine),
      eventBus_(eventBus),
            telemetry_(telemetry),
            environmentAdapter_(std::move(environmentAdapter)) {
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
    config_.decisionBudgetMs = ClampDecisionBudgetMs(config_.decisionBudgetMs);
    decisionBudget_ = std::chrono::milliseconds(config_.decisionBudgetMs);

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
    latestObservationSequence_ = 0;
    observationSamples_ = 0;
    lastObservationCaptureMs_ = 0;
    latestScreenFrameId_ = 0;
    lastVisionCaptureMs_ = 0;
    lastVisionDetectionMs_ = 0;
    lastVisionMergeMs_ = 0;
    averageVisionCaptureMs_ = 0.0;
    averageVisionDetectionMs_ = 0.0;
    averageVisionMergeMs_ = 0.0;
    observationFps_ = 0.0;
    lastVisionLoggedSequence_ = 0;
    observationAdapterName_.clear();
    lastTraceId_.clear();
    decisionIntentsProduced_ = 0;
    decisionTimeouts_ = 0;
    feedbackSamples_ = 0;
    feedbackMismatches_ = 0;
    feedbackCorrections_ = 0;

    highQueue_.clear();
    mediumQueue_.clear();
    lowQueue_.clear();
    cycleLatenciesMs_.clear();
    feedbackHistory_.clear();

    stopRequested_ = false;
    running_ = true;

    {
        std::lock_guard<std::mutex> decisionLock(decisionMutex_);
        decisionStopRequested_ = false;
        decisionStatePending_ = false;
    }

    if (environmentAdapter_ == nullptr) {
        environmentAdapter_ = std::make_shared<RegistryEnvironmentAdapter>(registry_);
    }

    observationConfig_.sampleIntervalMs = config.observationIntervalMs;
    if (observationConfig_.sampleIntervalMs < 1) {
        observationConfig_.sampleIntervalMs = std::max(1, config_.targetFrameMs / 2);
    }

    std::string observationMessage;
    const bool observationStarted = observationPipeline_.Start(environmentAdapter_, observationConfig_, &observationMessage);
    if (!observationStarted) {
        running_ = false;
        stopRequested_ = true;
        if (message != nullptr) {
            *message = "control runtime failed to start observation pipeline: " + observationMessage;
        }
        return false;
    }

    observationAdapterName_ = environmentAdapter_->Name();

    if (!decisionWorker_.joinable()) {
        decisionWorker_ = std::thread(&ControlRuntime::DecisionLoop, this);
    }

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

    {
        std::lock_guard<std::mutex> decisionLock(decisionMutex_);
        decisionStopRequested_ = true;
        decisionStatePending_ = false;
        decisionCv_.notify_all();
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    if (decisionWorker_.joinable()) {
        decisionWorker_.join();
    }

    observationPipeline_.Stop();

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
    snapshot.latestObservationSequence = latestObservationSequence_;
    snapshot.observationSamples = observationSamples_;
    snapshot.lastObservationCaptureMs = lastObservationCaptureMs_;
    snapshot.latestScreenFrameId = latestScreenFrameId_;
    snapshot.lastVisionCaptureMs = lastVisionCaptureMs_;
    snapshot.lastVisionDetectionMs = lastVisionDetectionMs_;
    snapshot.lastVisionMergeMs = lastVisionMergeMs_;
    snapshot.averageVisionCaptureMs = averageVisionCaptureMs_;
    snapshot.averageVisionDetectionMs = averageVisionDetectionMs_;
    snapshot.averageVisionMergeMs = averageVisionMergeMs_;
    snapshot.observationFps = observationFps_;
    snapshot.observationAdapter = observationAdapterName_;
    snapshot.decisionIntentsProduced = decisionIntentsProduced_;
    snapshot.decisionTimeouts = decisionTimeouts_;
    snapshot.feedbackSamples = feedbackSamples_;
    snapshot.feedbackMismatches = feedbackMismatches_;
    snapshot.feedbackCorrections = feedbackCorrections_;
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
    queued.enqueuedAt = std::chrono::steady_clock::now();

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

bool ControlRuntime::LatestEnvironmentState(EnvironmentState* state) const {
    if (state == nullptr) {
        return false;
    }

    if (observationPipeline_.Latest(state)) {
        return true;
    }

    const ObserverSnapshot snapshot = registry_.LastSnapshot();
    if (!snapshot.valid) {
        return false;
    }

    *state = EnvironmentStateFromSnapshot(snapshot, false);
    state->perception = LightweightPerception::Analyze(*state);
    return true;
}

void ControlRuntime::SetDecisionProvider(std::shared_ptr<DecisionProvider> provider, int budgetMs) {
    {
        std::lock_guard<std::mutex> lock(decisionMutex_);
        decisionProvider_ = std::move(provider);
        decisionBudget_ = std::chrono::milliseconds(ClampDecisionBudgetMs(budgetMs));
        decisionStatePending_ = false;
    }

    decisionCv_.notify_all();
}

void ControlRuntime::SetPredictor(std::shared_ptr<Predictor> predictor) {
    std::lock_guard<std::mutex> lock(predictorMutex_);
    predictor_ = std::move(predictor);
}

void ControlRuntime::SetExecutionObserver(ExecutionObserver observer) {
    std::lock_guard<std::mutex> lock(executionObserverMutex_);
    executionObserver_ = std::move(observer);
}

bool ControlRuntime::Predict(const Intent& intent, StateSnapshot* predictedState, std::string* diagnostics) const {
    if (predictedState == nullptr) {
        if (diagnostics != nullptr) {
            *diagnostics = "prediction output is null";
        }
        return false;
    }

    StateSnapshot current;
    if (!LatestEnvironmentState(&current)) {
        if (diagnostics != nullptr) {
            *diagnostics = "environment state unavailable";
        }
        return false;
    }

    std::shared_ptr<Predictor> predictor;
    {
        std::lock_guard<std::mutex> lock(predictorMutex_);
        predictor = predictor_;
    }

    if (predictor != nullptr) {
        *predictedState = predictor->Predict(intent, current, diagnostics);
        return true;
    }

    *predictedState = current;
    predictedState->sequence = current.sequence + 1;
    predictedState->sourceSnapshotVersion = current.sourceSnapshotVersion;
    predictedState->capturedAt = std::chrono::system_clock::now();
    predictedState->simulated = true;
    predictedState->valid = current.valid;

    if (diagnostics != nullptr) {
        *diagnostics = "predictor_not_set; heuristic passthrough used";
    }

    return true;
}

std::vector<Feedback> ControlRuntime::RecentFeedback(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::size_t cappedLimit = std::max<std::size_t>(1U, limit);
    const std::size_t start = feedbackHistory_.size() > cappedLimit ? feedbackHistory_.size() - cappedLimit : 0U;

    std::vector<Feedback> result;
    result.reserve(feedbackHistory_.size() - start);
    for (std::size_t index = start; index < feedbackHistory_.size(); ++index) {
        result.push_back(feedbackHistory_[index]);
    }
    return result;
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

void ControlRuntime::SubmitDecisionState(const EnvironmentState& state, std::uint64_t frame) {
    {
        std::lock_guard<std::mutex> lock(decisionMutex_);
        if (decisionStopRequested_ || decisionProvider_ == nullptr) {
            return;
        }

        decisionPendingState_ = state;
        decisionPendingFrame_ = frame;
        decisionStatePending_ = true;
    }

    decisionCv_.notify_one();
}

void ControlRuntime::DecisionLoop() {
    while (true) {
        std::shared_ptr<DecisionProvider> provider;
        EnvironmentState state;
        std::uint64_t frame = 0;
        std::chrono::milliseconds budget(2);

        {
            std::unique_lock<std::mutex> lock(decisionMutex_);
            decisionCv_.wait(lock, [this]() {
                return decisionStopRequested_ || (decisionStatePending_ && decisionProvider_ != nullptr);
            });

            if (decisionStopRequested_) {
                break;
            }

            provider = decisionProvider_;
            state = decisionPendingState_;
            frame = decisionPendingFrame_;
            budget = decisionBudget_;
            decisionStatePending_ = false;
        }

        if (provider == nullptr) {
            continue;
        }

        std::string diagnostics;
        std::vector<Intent> proposed;

        const auto started = std::chrono::steady_clock::now();
        try {
            proposed = provider->Decide(state, budget, &diagnostics);
        } catch (const std::exception& ex) {
            diagnostics = ex.what();
            proposed.clear();
        } catch (...) {
            diagnostics = "decision provider failed with unknown exception";
            proposed.clear();
        }
        const auto finished = std::chrono::steady_clock::now();

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started);
        const bool timedOut = budget.count() > 0 && elapsedMs > budget;

        std::size_t accepted = 0;
        for (Intent& intent : proposed) {
            if (accepted >= kMaxDecisionIntentsPerPass) {
                break;
            }

            if (intent.action == IntentAction::Unknown) {
                continue;
            }

            if (intent.name.empty()) {
                intent.name = iee::ToString(intent.action);
            }
            if (intent.source.empty()) {
                intent.source = provider->Name();
            }

            intent.context.controlFrame = frame;
            if (intent.context.snapshotVersion == 0) {
                intent.context.snapshotVersion = state.sourceSnapshotVersion == 0
                    ? state.sequence
                    : state.sourceSnapshotVersion;
            }
            if (intent.context.snapshotTicks == 0) {
                intent.context.snapshotTicks = state.sequence;
            }
            if (intent.context.windowTitle.empty()) {
                intent.context.windowTitle = state.activeWindowTitle;
            }
            if (intent.context.application.empty()) {
                intent.context.application = state.activeProcessPath;
            }
            intent.context.cursor = state.cursorPosition;

            const ControlPriority enqueuePriority = ParseControlPriorityHint(intent);
            if (EnqueueIntent(intent, enqueuePriority)) {
                ++accepted;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            decisionIntentsProduced_ += accepted;
            if (timedOut) {
                ++decisionTimeouts_;
            }
        }

        if (!diagnostics.empty()) {
            telemetry_.LogFailure("decision-frame-" + std::to_string(frame), "DecisionProvider", diagnostics);
        }
    }
}

bool ControlRuntime::PopIntentLocked(
    Intent* intent,
    ControlPriority* priority,
    std::chrono::steady_clock::time_point* enqueuedAt) {
    if (!highQueue_.empty()) {
        const QueuedIntent queued = std::move(highQueue_.front());
        highQueue_.pop_front();
        *intent = queued.intent;
        *priority = queued.priority;
        if (enqueuedAt != nullptr) {
            *enqueuedAt = queued.enqueuedAt;
        }
        return true;
    }

    if (!mediumQueue_.empty()) {
        const QueuedIntent queued = std::move(mediumQueue_.front());
        mediumQueue_.pop_front();
        *intent = queued.intent;
        *priority = queued.priority;
        if (enqueuedAt != nullptr) {
            *enqueuedAt = queued.enqueuedAt;
        }
        return true;
    }

    if (!lowQueue_.empty()) {
        const QueuedIntent queued = std::move(lowQueue_.front());
        lowQueue_.pop_front();
        *intent = queued.intent;
        *priority = queued.priority;
        if (enqueuedAt != nullptr) {
            *enqueuedAt = queued.enqueuedAt;
        }
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

        EnvironmentState synchronizedState;
        const auto observationLookupStart = std::chrono::steady_clock::now();
        bool hasSynchronizedState = observationPipeline_.Latest(&synchronizedState);
        if (!hasSynchronizedState) {
            const ObserverSnapshot fallbackSnapshot = registry_.LastSnapshot();
            if (fallbackSnapshot.valid) {
                synchronizedState = EnvironmentStateFromSnapshot(fallbackSnapshot, false);
                synchronizedState.perception = LightweightPerception::Analyze(synchronizedState);
                hasSynchronizedState = true;
            }
        }
        const auto observationLookupEnd = std::chrono::steady_clock::now();
        const double observationLookupMs = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(observationLookupEnd - observationLookupStart).count()) /
            1000.0;

        const ObservationPipelineMetrics observationMetrics = observationPipeline_.Metrics();
        bool logVisionSample = false;
        VisionLatencySample visionSample;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            latestObservationSequence_ = hasSynchronizedState ? synchronizedState.sequence : 0;
            latestSnapshotVersion_ = hasSynchronizedState
                ? (synchronizedState.sourceSnapshotVersion == 0 ? synchronizedState.sequence : synchronizedState.sourceSnapshotVersion)
                : 0;
            observationSamples_ = observationMetrics.samples;
            lastObservationCaptureMs_ = observationMetrics.lastCaptureMs;
            latestScreenFrameId_ = observationMetrics.latestFrameId;
            lastVisionCaptureMs_ = observationMetrics.lastVisionCaptureMs;
            lastVisionDetectionMs_ = observationMetrics.lastVisionDetectionMs;
            lastVisionMergeMs_ = observationMetrics.lastVisionMergeMs;
            averageVisionCaptureMs_ = observationMetrics.averageVisionCaptureMs;
            averageVisionDetectionMs_ = observationMetrics.averageVisionDetectionMs;
            averageVisionMergeMs_ = observationMetrics.averageVisionMergeMs;
            observationFps_ = observationMetrics.estimatedFps;
            if (!observationMetrics.adapterName.empty()) {
                observationAdapterName_ = observationMetrics.adapterName;
            }

            if (hasSynchronizedState && synchronizedState.sequence > 0 && synchronizedState.sequence != lastVisionLoggedSequence_) {
                lastVisionLoggedSequence_ = synchronizedState.sequence;
                logVisionSample = true;

                visionSample.frameId = synchronizedState.screenState.frameId;
                visionSample.environmentSequence = synchronizedState.sequence;
                visionSample.captureMs = static_cast<double>(synchronizedState.visionTiming.captureMs);
                visionSample.detectionMs = static_cast<double>(synchronizedState.visionTiming.detectionMs);
                visionSample.mergeMs = static_cast<double>(synchronizedState.visionTiming.mergeMs);
                visionSample.totalMs = static_cast<double>(synchronizedState.visionTiming.totalMs);
                visionSample.simulated = synchronizedState.screenState.simulated;
                visionSample.timestamp = synchronizedState.capturedAt;
            }
        }

        if (logVisionSample) {
            telemetry_.LogVisionSample(visionSample);
        }

        if (hasSynchronizedState) {
            SubmitDecisionState(synchronizedState, currentFrame);
        }

        Intent intent;
        ControlPriority priority{ControlPriority::Low};
        bool hasIntent = false;
        std::chrono::steady_clock::time_point enqueuedAt = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hasIntent = PopIntentLocked(&intent, &priority, &enqueuedAt);
        }

        if (hasIntent) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - frameStart)
                                     .count();
            const int remainingBudgetMs = frameMs - static_cast<int>(elapsed);

            if (remainingBudgetMs > 0) {
                intent.context.controlFrame = currentFrame;
                if (hasSynchronizedState) {
                    const std::uint64_t synchronizedSnapshotVersion =
                        synchronizedState.sourceSnapshotVersion == 0
                        ? synchronizedState.sequence
                        : synchronizedState.sourceSnapshotVersion;

                    if (intent.context.snapshotVersion == 0) {
                        intent.context.snapshotVersion = synchronizedSnapshotVersion;
                    }
                    if (intent.context.snapshotTicks == 0 && synchronizedState.sequence > 0) {
                        intent.context.snapshotTicks = synchronizedState.sequence;
                    }
                    if (intent.context.windowTitle.empty()) {
                        intent.context.windowTitle = synchronizedState.activeWindowTitle;
                    }
                    if (intent.context.application.empty()) {
                        intent.context.application = synchronizedState.activeProcessPath;
                    }
                    intent.context.cursor = synchronizedState.cursorPosition;
                }

                const double queueWaitMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - enqueuedAt)
                        .count()) /
                    1000.0;

                const auto executeStarted = std::chrono::steady_clock::now();
                const ExecutionResult result =
                    executionEngine_.ExecuteWithBudget(intent, std::chrono::milliseconds(remainingBudgetMs));
                const auto executeFinished = std::chrono::steady_clock::now();

                if (result.IsSuccess() && !intent.id.empty()) {
                    registry_.RecordInteraction(intent.id);
                }

                const double executionWallMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(executeFinished - executeStarted).count()) /
                    1000.0;
                const double totalMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(executeFinished - frameStart).count()) /
                    1000.0;

                LatencyBreakdownSample breakdown;
                breakdown.frame = currentFrame;
                breakdown.traceId = result.traceId;
                breakdown.observationMs = std::max(observationLookupMs, static_cast<double>(observationMetrics.lastCaptureMs));
                breakdown.perceptionMs = hasSynchronizedState
                    ? static_cast<double>(synchronizedState.perception.computeMs)
                    : 0.0;
                breakdown.queueWaitMs = std::max(0.0, queueWaitMs);
                breakdown.executionMs = std::max(static_cast<double>(result.duration.count()), executionWallMs);
                breakdown.verificationMs = 0.0;
                breakdown.totalMs = std::max(0.0, totalMs);
                breakdown.timestamp = std::chrono::system_clock::now();
                telemetry_.LogLatencyBreakdown(breakdown);

                EnvironmentState afterState;
                bool hasAfterState = false;
                if (hasSynchronizedState) {
                    hasAfterState = observationPipeline_.Latest(&afterState);
                    if (!hasAfterState) {
                        const ObserverSnapshot fallbackSnapshot = registry_.LastSnapshot();
                        if (fallbackSnapshot.valid) {
                            afterState = EnvironmentStateFromSnapshot(fallbackSnapshot, false);
                            afterState.perception = LightweightPerception::Analyze(afterState);
                            hasAfterState = true;
                        }
                    }
                }

                bool mismatch = false;
                std::optional<FeedbackDelta> feedbackDelta;
                if (hasSynchronizedState && hasAfterState) {
                    Feedback feedback;
                    feedback.intent = intent;
                    feedback.status = result.status;
                    feedback.before = synchronizedState;
                    feedback.after = afterState;
                    feedback.delta = ComputeFeedbackDelta(synchronizedState, afterState);
                    feedback.traceId = result.traceId;
                    feedback.frame = currentFrame;
                    feedback.timestamp = std::chrono::system_clock::now();
                    feedback.mismatch = result.IsSuccess() && IsLikelyMismatch(intent, feedback.delta, synchronizedState, afterState);
                    mismatch = feedback.mismatch;
                    feedbackDelta = feedback.delta;

                    std::lock_guard<std::mutex> lock(mutex_);
                    feedbackHistory_.push_back(std::move(feedback));
                    if (feedbackHistory_.size() > kMaxFeedbackHistory) {
                        feedbackHistory_.pop_front();
                    }
                    ++feedbackSamples_;
                    if (mismatch) {
                        ++feedbackMismatches_;
                    }
                }

                if (mismatch && result.IsSuccess() && ShouldScheduleCorrection(intent)) {
                    Intent correctiveIntent = intent;
                    correctiveIntent.params.values["feedback_retry"] = L"1";
                    if (EnqueueIntent(correctiveIntent, ControlPriority::High)) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        ++feedbackCorrections_;
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++intentsExecuted_;
                    lastTraceId_ = result.traceId;
                }

                ExecutionObserver observer;
                {
                    std::lock_guard<std::mutex> lock(executionObserverMutex_);
                    observer = executionObserver_;
                }
                if (observer) {
                    observer(intent, result, feedbackDelta, mismatch);
                }
            } else {
                QueuedIntent queued;
                queued.intent = intent;
                queued.priority = priority;
                queued.sequence = 0;
                queued.enqueuedAt = enqueuedAt;

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
    stream << "\"observation\":{";
    stream << "\"adapter\":\"" << EscapeJson(snapshot.observationAdapter) << "\",";
    stream << "\"latest_sequence\":" << snapshot.latestObservationSequence << ",";
    stream << "\"samples\":" << snapshot.observationSamples << ",";
    stream << "\"last_capture_ms\":" << snapshot.lastObservationCaptureMs << ",";
    stream << "\"latest_frame_id\":" << snapshot.latestScreenFrameId << ",";
    stream << "\"fps\":" << snapshot.observationFps << ",";
    stream << "\"vision\":{";
    stream << "\"last_capture_ms\":" << snapshot.lastVisionCaptureMs << ",";
    stream << "\"last_detection_ms\":" << snapshot.lastVisionDetectionMs << ",";
    stream << "\"last_merge_ms\":" << snapshot.lastVisionMergeMs << ",";
    stream << "\"avg_capture_ms\":" << snapshot.averageVisionCaptureMs << ",";
    stream << "\"avg_detection_ms\":" << snapshot.averageVisionDetectionMs << ",";
    stream << "\"avg_merge_ms\":" << snapshot.averageVisionMergeMs;
    stream << "}";
    stream << "},";
    stream << "\"decision\":{";
    stream << "\"produced\":" << snapshot.decisionIntentsProduced << ",";
    stream << "\"timeouts\":" << snapshot.decisionTimeouts;
    stream << "},";
    stream << "\"feedback\":{";
    stream << "\"samples\":" << snapshot.feedbackSamples << ",";
    stream << "\"mismatches\":" << snapshot.feedbackMismatches << ",";
    stream << "\"corrections\":" << snapshot.feedbackCorrections;
    stream << "},";
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
