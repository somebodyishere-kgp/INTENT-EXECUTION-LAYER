#pragma once

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "UiElement.h"

namespace iee {

struct ScreenFrame {
    std::uint64_t frameId{0};
    std::chrono::system_clock::time_point capturedAt{std::chrono::system_clock::now()};
    int width{0};
    int height{0};
    bool simulated{false};
    bool valid{false};
};

struct VisualElement {
    std::string id;
    std::string kind;
    RECT bounds{0, 0, 0, 0};
    std::uint32_t colorCluster{0};
    double edgeDensity{0.0};
    bool textLike{false};
    double confidence{0.0};
};

struct ScreenElement {
    std::string id;
    std::string source;
    std::string uiElementId;
    std::wstring label;
    RECT bounds{0, 0, 0, 0};
    double confidence{0.0};
    bool focused{false};
    bool textLike{false};
};

struct ScreenState {
    std::uint64_t frameId{0};
    std::uint64_t environmentSequence{0};
    std::chrono::system_clock::time_point capturedAt{std::chrono::system_clock::now()};
    POINT cursorPosition{0, 0};
    int width{0};
    int height{0};
    std::uint64_t signature{0};
    std::vector<VisualElement> visualElements;
    std::vector<ScreenElement> elements;
    bool simulated{false};
    bool valid{false};
};

struct VisionTiming {
    std::int64_t captureMs{0};
    std::int64_t detectionMs{0};
    std::int64_t mergeMs{0};
    std::int64_t totalMs{0};
};

class ScreenCaptureEngine {
public:
    ScreenCaptureEngine();
    ~ScreenCaptureEngine();

    bool Capture(ScreenFrame* frame, std::string* error = nullptr);

private:
    struct Impl;

    bool Initialize(std::string* error);
    void Shutdown();

    Impl* impl_{nullptr};
    bool initialized_{false};
    bool duplicationAvailable_{false};
    std::uint64_t frameCounter_{0};
};

class VisualDetector {
public:
    static std::vector<VisualElement> Detect(const ScreenFrame& frame, const std::vector<UiElement>& uiElements);
};

class ScreenStateAssembler {
public:
    static ScreenState Build(
        std::uint64_t environmentSequence,
        std::chrono::system_clock::time_point capturedAt,
        POINT cursorPosition,
        const std::vector<UiElement>& uiElements,
        const ScreenFrame& frame,
        const std::vector<VisualElement>& visualElements);
};

}  // namespace iee
