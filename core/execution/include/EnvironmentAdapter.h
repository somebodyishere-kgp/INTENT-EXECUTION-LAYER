#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ObserverEngine.h"

namespace iee {

class IntentRegistry;

struct EnvironmentRegion {
    RECT bounds{0, 0, 0, 0};
    std::size_t elementCount{0};
    bool hasFocus{false};
};

struct EnvironmentPerception {
    std::string dominantSurface{"unknown"};
    double focusRatio{0.0};
    double occupancyRatio{0.0};
    std::uint64_t uiSignature{0};
    std::int64_t computeMs{0};
    std::vector<EnvironmentRegion> regions;
};

struct EnvironmentState {
    std::uint64_t sequence{0};
    std::uint64_t sourceSnapshotVersion{0};
    std::chrono::system_clock::time_point capturedAt{std::chrono::system_clock::now()};
    std::wstring activeWindowTitle;
    std::wstring activeProcessPath;
    POINT cursorPosition{0, 0};
    std::vector<UiElement> uiElements;
    std::vector<FileSystemEntry> fileSystemEntries;
    EnvironmentPerception perception;
    bool simulated{false};
    bool valid{false};
};

EnvironmentState EnvironmentStateFromSnapshot(const ObserverSnapshot& snapshot, bool simulated = false);

class LightweightPerception {
public:
    static EnvironmentPerception Analyze(const EnvironmentState& state);
};

class EnvironmentAdapter {
public:
    virtual ~EnvironmentAdapter() = default;

    virtual std::string Name() const = 0;
    virtual bool CaptureState(EnvironmentState* state, std::string* error = nullptr) = 0;
};

class RegistryEnvironmentAdapter final : public EnvironmentAdapter {
public:
    explicit RegistryEnvironmentAdapter(IntentRegistry& registry);

    std::string Name() const override;
    bool CaptureState(EnvironmentState* state, std::string* error = nullptr) override;

private:
    IntentRegistry& registry_;
};

class MockEnvironmentAdapter final : public EnvironmentAdapter {
public:
    MockEnvironmentAdapter();
    explicit MockEnvironmentAdapter(std::vector<EnvironmentState> scriptedStates);

    std::string Name() const override;
    bool CaptureState(EnvironmentState* state, std::string* error = nullptr) override;

    void SetScriptedStates(std::vector<EnvironmentState> states);
    void PushState(const EnvironmentState& state);
    void SetLooping(bool loop);

private:
    mutable std::mutex mutex_;
    std::vector<EnvironmentState> states_;
    std::size_t nextIndex_{0};
    std::uint64_t generatedSequence_{0};
    bool loop_{true};
};

struct ObservationPipelineConfig {
    int sampleIntervalMs{8};
};

struct ObservationPipelineMetrics {
    bool running{false};
    std::uint64_t samples{0};
    std::uint64_t captureFailures{0};
    std::int64_t lastCaptureMs{0};
    double averageCaptureMs{0.0};
    std::uint64_t latestSequence{0};
    std::string adapterName;
};

class ObservationPipeline {
public:
    ObservationPipeline();
    ~ObservationPipeline();

    bool Start(
        std::shared_ptr<EnvironmentAdapter> adapter,
        const ObservationPipelineConfig& config,
        std::string* message = nullptr);
    void Stop();

    bool Running() const;
    bool Latest(EnvironmentState* state) const;
    ObservationPipelineMetrics Metrics() const;

private:
    void RunLoop();

    mutable std::mutex mutex_;
    std::condition_variable wakeCv_;
    std::thread worker_;

    std::shared_ptr<EnvironmentAdapter> adapter_;
    ObservationPipelineConfig config_;
    bool running_{false};
    bool stopRequested_{false};

    std::array<EnvironmentState, 2> buffers_;
    std::array<bool, 2> bufferValid_{false, false};
    std::atomic<int> activeBufferIndex_{0};

    std::uint64_t generatedSequence_{0};
    std::uint64_t samples_{0};
    std::uint64_t captureFailures_{0};
    std::int64_t lastCaptureMs_{0};
    double totalCaptureMs_{0.0};
    std::uint64_t latestSequence_{0};
};

}  // namespace iee
