#include "EnvironmentAdapter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <unordered_map>

#include "IntentRegistry.h"

namespace iee {
namespace {

std::size_t HashCombine(std::size_t seed, std::size_t value) {
    return seed ^ (value + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) + (seed << 6U) + (seed >> 2U));
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

std::uint64_t BuildUnifiedSignature(const ScreenState& screenState, const InteractionGraph& interactionGraph) {
    std::size_t signature = static_cast<std::size_t>(0xcbf29ce484222325ULL);
    signature = HashCombine(signature, std::hash<std::uint64_t>{}(screenState.signature));
    signature = HashCombine(signature, std::hash<std::uint64_t>{}(interactionGraph.signature));
    signature = HashCombine(signature, std::hash<std::size_t>{}(screenState.elements.size()));
    signature = HashCombine(signature, std::hash<std::size_t>{}(interactionGraph.nodes.size()));
    return static_cast<std::uint64_t>(signature == 0 ? 1 : signature);
}

void PopulateUnifiedState(EnvironmentState* state, bool forceRebuildInteractionGraph) {
    if (state == nullptr) {
        return;
    }

    if (state->screenState.frameId == 0) {
        state->screenState.frameId = state->screenFrame.frameId == 0 ? state->sequence : state->screenFrame.frameId;
    }
    if (state->screenState.environmentSequence == 0) {
        state->screenState.environmentSequence = state->sequence;
    }

    const bool needsGraphRefresh = forceRebuildInteractionGraph ||
        !state->unifiedState.interactionGraph.valid ||
        state->unifiedState.interactionGraph.sequence != state->sequence;

    if (needsGraphRefresh) {
        state->unifiedState.interactionGraph = InteractionGraphBuilder::Build(state->uiElements, state->sequence);
    }

    state->unifiedState.frameId = state->screenState.frameId;
    state->unifiedState.environmentSequence = state->sequence;
    state->unifiedState.capturedAt = state->capturedAt;
    state->unifiedState.screenState = state->screenState;
    state->unifiedState.signature =
        BuildUnifiedSignature(state->unifiedState.screenState, state->unifiedState.interactionGraph);
    state->unifiedState.valid = state->screenState.valid &&
        (state->uiElements.empty() || state->unifiedState.interactionGraph.valid);
}

double RectArea(const RECT& rect) {
    const int width = std::max(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max(0, static_cast<int>(rect.bottom - rect.top));
    return static_cast<double>(width) * static_cast<double>(height);
}

std::string ClassifyDominantSurface(const std::unordered_map<UiControlType, std::size_t>& typeCounts) {
    auto countOf = [&typeCounts](UiControlType type) {
        const auto it = typeCounts.find(type);
        return it == typeCounts.end() ? 0U : it->second;
    };

    const std::size_t formScore = countOf(UiControlType::TextBox) + countOf(UiControlType::ComboBox);
    const std::size_t commandScore = countOf(UiControlType::Button);
    const std::size_t navigationScore = countOf(UiControlType::Menu) + countOf(UiControlType::MenuItem);
    const std::size_t listScore = countOf(UiControlType::ListItem);

    std::size_t best = formScore;
    std::string dominant = "form";

    if (commandScore > best) {
        best = commandScore;
        dominant = "command";
    }
    if (navigationScore > best) {
        best = navigationScore;
        dominant = "navigation";
    }
    if (listScore > best) {
        best = listScore;
        dominant = "list";
    }

    if (best == 0U) {
        return "unknown";
    }

    return dominant;
}

void PopulateScreenState(EnvironmentState* state, ScreenCaptureEngine* captureEngine, std::mutex* captureMutex) {
    if (state == nullptr) {
        return;
    }

    const auto totalStart = std::chrono::steady_clock::now();

    const auto captureStart = std::chrono::steady_clock::now();
    ScreenFrame frame;
    std::string captureError;
    bool captureOk = false;

    if (captureEngine != nullptr && captureMutex != nullptr) {
        std::lock_guard<std::mutex> lock(*captureMutex);
        captureOk = captureEngine->Capture(&frame, &captureError);
    }

    if (!captureOk || !frame.valid) {
        frame.frameId = state->sequence;
        frame.capturedAt = state->capturedAt;
        frame.width = std::max(1, GetSystemMetrics(SM_CXSCREEN));
        frame.height = std::max(1, GetSystemMetrics(SM_CYSCREEN));
        frame.simulated = true;
        frame.valid = true;
    }
    const auto captureEnd = std::chrono::steady_clock::now();

    const auto detectStart = captureEnd;
    const std::vector<VisualElement> visualElements = VisualDetector::Detect(frame, state->uiElements);
    const auto detectEnd = std::chrono::steady_clock::now();

    const auto mergeStart = detectEnd;
    state->screenState = ScreenStateAssembler::Build(
        state->sequence,
        state->capturedAt,
        state->cursorPosition,
        state->uiElements,
        frame,
        visualElements);
    const auto mergeEnd = std::chrono::steady_clock::now();

    state->screenFrame = std::move(frame);
    state->visionTiming.captureMs = std::chrono::duration_cast<std::chrono::milliseconds>(captureEnd - captureStart).count();
    state->visionTiming.detectionMs = std::chrono::duration_cast<std::chrono::milliseconds>(detectEnd - detectStart).count();
    state->visionTiming.mergeMs = std::chrono::duration_cast<std::chrono::milliseconds>(mergeEnd - mergeStart).count();
    state->visionTiming.totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(mergeEnd - totalStart).count();

    PopulateUnifiedState(state, true);
}

}  // namespace

EnvironmentState EnvironmentStateFromSnapshot(const ObserverSnapshot& snapshot, bool simulated) {
    EnvironmentState state;
    state.sequence = snapshot.sequence;
    state.sourceSnapshotVersion = snapshot.sequence;
    state.capturedAt = snapshot.capturedAt;
    state.activeWindowTitle = snapshot.activeWindowTitle;
    state.activeProcessPath = snapshot.activeProcessPath;
    state.cursorPosition = snapshot.cursorPosition;
    state.uiElements = snapshot.uiElements;
    state.fileSystemEntries = snapshot.fileSystemEntries;
    state.simulated = simulated;
    state.valid = snapshot.valid;
    return state;
}

EnvironmentPerception LightweightPerception::Analyze(const EnvironmentState& state) {
    const auto start = std::chrono::steady_clock::now();

    EnvironmentPerception perception;
    if (state.uiElements.empty()) {
        const auto end = std::chrono::steady_clock::now();
        perception.computeMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        return perception;
    }

    const int screenWidth = std::max(1, GetSystemMetrics(SM_CXSCREEN));
    const int screenHeight = std::max(1, GetSystemMetrics(SM_CYSCREEN));
    const double totalScreenArea = static_cast<double>(screenWidth) * static_cast<double>(screenHeight);

    std::unordered_map<UiControlType, std::size_t> typeCounts;
    typeCounts.reserve(8U);

    std::array<EnvironmentRegion, 9> regions;
    const int regionWidth = std::max(1, screenWidth / 3);
    const int regionHeight = std::max(1, screenHeight / 3);

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EnvironmentRegion& region = regions[static_cast<std::size_t>((row * 3) + col)];
            region.bounds.left = col * regionWidth;
            region.bounds.top = row * regionHeight;
            region.bounds.right = (col == 2) ? screenWidth : ((col + 1) * regionWidth);
            region.bounds.bottom = (row == 2) ? screenHeight : ((row + 1) * regionHeight);
        }
    }

    std::size_t focusedCount = 0;
    double occupiedArea = 0.0;
    std::size_t signature = static_cast<std::size_t>(0xcbf29ce484222325ULL);
    std::unordered_map<std::string, std::size_t> groupingByParent;
    groupingByParent.reserve(state.uiElements.size());

    for (const UiElement& element : state.uiElements) {
        ++typeCounts[element.controlType];

        const std::string lowerName = ToAsciiLower(Narrow(element.name));
        const bool textHeuristic =
            !lowerName.empty() &&
            std::any_of(lowerName.begin(), lowerName.end(), [](unsigned char ch) {
                return std::isalpha(ch) != 0;
            });
        if (textHeuristic || element.controlType == UiControlType::TextBox) {
            ++perception.lightweightTextDetections;
        }

        const std::string groupKey = element.parentId.empty() ? element.id : element.parentId;
        if (!groupKey.empty()) {
            ++groupingByParent[groupKey];
        }

        if (element.isFocused) {
            ++focusedCount;
        }

        occupiedArea += RectArea(element.bounds);

        const int centerX = element.bounds.left + ((element.bounds.right - element.bounds.left) / 2);
        const int centerY = element.bounds.top + ((element.bounds.bottom - element.bounds.top) / 2);
        const int clampedX = std::clamp(centerX, 0, std::max(0, screenWidth - 1));
        const int clampedY = std::clamp(centerY, 0, std::max(0, screenHeight - 1));

        const int regionX = std::clamp((clampedX * 3) / screenWidth, 0, 2);
        const int regionY = std::clamp((clampedY * 3) / screenHeight, 0, 2);
        EnvironmentRegion& region = regions[static_cast<std::size_t>((regionY * 3) + regionX)];
        ++region.elementCount;
        region.hasFocus = region.hasFocus || element.isFocused;

        signature = HashCombine(signature, std::hash<std::string>{}(element.id));
        signature = HashCombine(signature, std::hash<std::wstring>{}(element.name));
        signature = HashCombine(signature, std::hash<int>{}(element.bounds.left));
        signature = HashCombine(signature, std::hash<int>{}(element.bounds.top));
        signature = HashCombine(signature, std::hash<int>{}(element.bounds.right));
        signature = HashCombine(signature, std::hash<int>{}(element.bounds.bottom));
    }

    perception.dominantSurface = ClassifyDominantSurface(typeCounts);
    perception.focusRatio =
        static_cast<double>(focusedCount) / static_cast<double>(state.uiElements.size());
    perception.occupancyRatio = std::clamp(occupiedArea / std::max(1.0, totalScreenArea), 0.0, 1.0);
    perception.uiSignature = static_cast<std::uint64_t>(signature);

    for (const EnvironmentRegion& region : regions) {
        if (region.elementCount == 0U) {
            continue;
        }
        perception.regions.push_back(region);

        std::string label = "sparse";
        if (region.hasFocus) {
            label = "focus";
        } else if (region.elementCount >= 6U) {
            label = "dense";
        } else if (region.elementCount >= 3U) {
            label = "interactive";
        }
        perception.regionLabels.push_back(label);
    }

    for (const auto& group : groupingByParent) {
        if (group.second > 1U) {
            ++perception.groupedRegionCount;
        }
    }

    if (perception.regionLabels.empty() && !perception.dominantSurface.empty()) {
        perception.regionLabels.push_back(perception.dominantSurface);
    }

    const auto end = std::chrono::steady_clock::now();
    perception.computeMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return perception;
}

RegistryEnvironmentAdapter::RegistryEnvironmentAdapter(IntentRegistry& registry)
    : registry_(registry) {}

std::string RegistryEnvironmentAdapter::Name() const {
    return "RegistryEnvironmentAdapter";
}

bool RegistryEnvironmentAdapter::CaptureState(EnvironmentState* state, std::string* error) {
    if (state == nullptr) {
        if (error != nullptr) {
            *error = "EnvironmentState output is null";
        }
        return false;
    }

    ObserverSnapshot snapshot = registry_.LastSnapshot();
    if (!snapshot.valid || snapshot.sequence == 0) {
        registry_.Refresh();
        snapshot = registry_.LastSnapshot();
    }

    *state = EnvironmentStateFromSnapshot(snapshot, false);
    state->perception = LightweightPerception::Analyze(*state);
    PopulateScreenState(state, &screenCaptureEngine_, &screenCaptureMutex_);

    if (!state->valid) {
        if (error != nullptr) {
            *error = "Observer snapshot is not valid";
        }
        return false;
    }

    return true;
}

MockEnvironmentAdapter::MockEnvironmentAdapter() = default;

MockEnvironmentAdapter::MockEnvironmentAdapter(std::vector<EnvironmentState> scriptedStates)
    : states_(std::move(scriptedStates)) {}

std::string MockEnvironmentAdapter::Name() const {
    return "MockEnvironmentAdapter";
}

bool MockEnvironmentAdapter::CaptureState(EnvironmentState* state, std::string* error) {
    if (state == nullptr) {
        if (error != nullptr) {
            *error = "EnvironmentState output is null";
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (states_.empty()) {
        if (error != nullptr) {
            *error = "No scripted environment states";
        }
        return false;
    }

    if (nextIndex_ >= states_.size()) {
        if (loop_) {
            nextIndex_ = 0;
        } else {
            nextIndex_ = states_.size() - 1U;
        }
    }

    EnvironmentState selected = states_[nextIndex_];
    if (loop_ || (nextIndex_ + 1U) < states_.size()) {
        ++nextIndex_;
    }

    selected.simulated = true;
    selected.valid = true;
    if (selected.sequence == 0) {
        selected.sequence = ++generatedSequence_;
    } else {
        generatedSequence_ = std::max(generatedSequence_, selected.sequence);
    }

    selected.sourceSnapshotVersion =
        selected.sourceSnapshotVersion == 0 ? selected.sequence : selected.sourceSnapshotVersion;

    selected.perception = LightweightPerception::Analyze(selected);
    PopulateScreenState(&selected, nullptr, nullptr);

    *state = std::move(selected);
    return true;
}

void MockEnvironmentAdapter::SetScriptedStates(std::vector<EnvironmentState> states) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_ = std::move(states);
    nextIndex_ = 0;
    generatedSequence_ = 0;
}

void MockEnvironmentAdapter::PushState(const EnvironmentState& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_.push_back(state);
}

void MockEnvironmentAdapter::SetLooping(bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = loop;
}

ObservationPipeline::ObservationPipeline() = default;

ObservationPipeline::~ObservationPipeline() {
    Stop();
}

bool ObservationPipeline::Start(
    std::shared_ptr<EnvironmentAdapter> adapter,
    const ObservationPipelineConfig& config,
    std::string* message) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        if (message != nullptr) {
            *message = "observation pipeline already running";
        }
        return false;
    }

    if (adapter == nullptr) {
        if (message != nullptr) {
            *message = "environment adapter is required";
        }
        return false;
    }

    adapter_ = std::move(adapter);
    config_ = config;
    if (config_.sampleIntervalMs < 1) {
        config_.sampleIntervalMs = 1;
    }

    stopRequested_ = false;
    running_ = true;

    generatedSequence_ = 0;
    samples_ = 0;
    captureFailures_ = 0;
    lastCaptureMs_ = 0;
    totalCaptureMs_ = 0.0;
    latestSequence_ = 0;
    latestFrameId_ = 0;
    lastVisionCaptureMs_ = 0;
    lastVisionDetectionMs_ = 0;
    lastVisionMergeMs_ = 0;
    totalVisionCaptureMs_ = 0.0;
    totalVisionDetectionMs_ = 0.0;
    totalVisionMergeMs_ = 0.0;
    startedAt_ = std::chrono::steady_clock::now();

    bufferValid_[0] = false;
    bufferValid_[1] = false;
    activeBufferIndex_.store(0, std::memory_order_release);

    worker_ = std::thread(&ObservationPipeline::RunLoop, this);

    if (message != nullptr) {
        *message = "observation pipeline started";
    }
    return true;
}

void ObservationPipeline::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        stopRequested_ = true;
        wakeCv_.notify_all();
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

bool ObservationPipeline::Running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

bool ObservationPipeline::Latest(EnvironmentState* state) const {
    if (state == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const int index = activeBufferIndex_.load(std::memory_order_acquire);
    if (index < 0 || index > 1 || !bufferValid_[static_cast<std::size_t>(index)]) {
        return false;
    }

    *state = buffers_[static_cast<std::size_t>(index)];
    return true;
}

ObservationPipelineMetrics ObservationPipeline::Metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);

    ObservationPipelineMetrics metrics;
    metrics.running = running_;
    metrics.samples = samples_;
    metrics.captureFailures = captureFailures_;
    metrics.lastCaptureMs = lastCaptureMs_;
    metrics.latestSequence = latestSequence_;
    metrics.latestFrameId = latestFrameId_;
    metrics.lastVisionCaptureMs = lastVisionCaptureMs_;
    metrics.lastVisionDetectionMs = lastVisionDetectionMs_;
    metrics.lastVisionMergeMs = lastVisionMergeMs_;
    metrics.adapterName = adapter_ != nullptr ? adapter_->Name() : "";

    const double denominator = static_cast<double>(samples_ + captureFailures_);
    if (denominator > 0.0) {
        metrics.averageCaptureMs = totalCaptureMs_ / denominator;
    }

    const double sampleDenominator = static_cast<double>(std::max<std::uint64_t>(1ULL, samples_));
    metrics.averageVisionCaptureMs = totalVisionCaptureMs_ / sampleDenominator;
    metrics.averageVisionDetectionMs = totalVisionDetectionMs_ / sampleDenominator;
    metrics.averageVisionMergeMs = totalVisionMergeMs_ / sampleDenominator;

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt_)
                             .count();
    if (elapsed > 0) {
        metrics.estimatedFps = (static_cast<double>(samples_) * 1000.0) / static_cast<double>(elapsed);
    }

    return metrics;
}

void ObservationPipeline::RunLoop() {
    auto nextCapture = std::chrono::steady_clock::now();

    while (true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopRequested_) {
                break;
            }
        }

        EnvironmentState captured;
        std::string error;

        const auto captureStart = std::chrono::steady_clock::now();
        const bool ok = adapter_ != nullptr && adapter_->CaptureState(&captured, &error);
        const auto captureEnd = std::chrono::steady_clock::now();
        const std::int64_t captureMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(captureEnd - captureStart).count();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            lastCaptureMs_ = captureMs;
            totalCaptureMs_ += static_cast<double>(captureMs);

            if (ok && captured.valid) {
                if (captured.sequence == 0) {
                    captured.sequence = ++generatedSequence_;
                } else {
                    generatedSequence_ = std::max(generatedSequence_, captured.sequence);
                }

                captured.sourceSnapshotVersion =
                    captured.sourceSnapshotVersion == 0 ? captured.sequence : captured.sourceSnapshotVersion;

                if (captured.screenFrame.frameId == 0) {
                    captured.screenFrame.frameId = captured.sequence;
                }
                if (!captured.screenFrame.valid) {
                    captured.screenFrame.width = std::max(1, GetSystemMetrics(SM_CXSCREEN));
                    captured.screenFrame.height = std::max(1, GetSystemMetrics(SM_CYSCREEN));
                    captured.screenFrame.simulated = true;
                    captured.screenFrame.valid = true;
                }
                if (captured.screenState.frameId == 0) {
                    captured.screenState.frameId = captured.screenFrame.frameId;
                }
                if (captured.screenState.environmentSequence == 0) {
                    captured.screenState.environmentSequence = captured.sequence;
                }

                PopulateUnifiedState(&captured, false);

                const int current = activeBufferIndex_.load(std::memory_order_relaxed);
                const int next = current == 0 ? 1 : 0;
                buffers_[static_cast<std::size_t>(next)] = std::move(captured);
                bufferValid_[static_cast<std::size_t>(next)] = true;
                activeBufferIndex_.store(next, std::memory_order_release);

                ++samples_;
                latestSequence_ = buffers_[static_cast<std::size_t>(next)].sequence;
                latestFrameId_ = buffers_[static_cast<std::size_t>(next)].screenState.frameId;
                lastVisionCaptureMs_ = buffers_[static_cast<std::size_t>(next)].visionTiming.captureMs;
                lastVisionDetectionMs_ = buffers_[static_cast<std::size_t>(next)].visionTiming.detectionMs;
                lastVisionMergeMs_ = buffers_[static_cast<std::size_t>(next)].visionTiming.mergeMs;
                totalVisionCaptureMs_ += static_cast<double>(lastVisionCaptureMs_);
                totalVisionDetectionMs_ += static_cast<double>(lastVisionDetectionMs_);
                totalVisionMergeMs_ += static_cast<double>(lastVisionMergeMs_);
            } else {
                ++captureFailures_;
            }
        }

        nextCapture += std::chrono::milliseconds(std::max(1, config_.sampleIntervalMs));

        std::unique_lock<std::mutex> waitLock(mutex_);
        if (stopRequested_) {
            break;
        }

        wakeCv_.wait_until(waitLock, nextCapture, [this]() {
            return stopRequested_;
        });

        const auto now = std::chrono::steady_clock::now();
        if (now > nextCapture + std::chrono::milliseconds(config_.sampleIntervalMs)) {
            nextCapture = now;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    stopRequested_ = false;
}

}  // namespace iee
