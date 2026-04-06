#include "ScreenPerception.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

namespace iee {
namespace {

using Microsoft::WRL::ComPtr;

std::size_t HashCombine(std::size_t seed, std::size_t value) {
    return seed ^ (value + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) + (seed << 6U) + (seed >> 2U));
}

double RectArea(const RECT& rect) {
    const int width = std::max<int>(0, static_cast<int>(rect.right - rect.left));
    const int height = std::max<int>(0, static_cast<int>(rect.bottom - rect.top));
    return static_cast<double>(width) * static_cast<double>(height);
}

RECT ClampRectToFrame(const RECT& rect, int width, int height) {
    RECT clamped = rect;
    const LONG minCoord = 0;
    const LONG maxLeft = static_cast<LONG>(std::max(0, width - 1));
    const LONG maxTop = static_cast<LONG>(std::max(0, height - 1));
    clamped.left = std::clamp<LONG>(clamped.left, minCoord, maxLeft);
    clamped.top = std::clamp<LONG>(clamped.top, minCoord, maxTop);

    const LONG minRight = static_cast<LONG>(clamped.left + 1);
    const LONG minBottom = static_cast<LONG>(clamped.top + 1);
    const LONG maxRight = static_cast<LONG>(std::max<int>(static_cast<int>(minRight), width));
    const LONG maxBottom = static_cast<LONG>(std::max<int>(static_cast<int>(minBottom), height));
    clamped.right = std::clamp<LONG>(clamped.right, minRight, maxRight);
    clamped.bottom = std::clamp<LONG>(clamped.bottom, minBottom, maxBottom);
    return clamped;
}

RECT UnionRects(const RECT& left, const RECT& right) {
    RECT merged{};
    merged.left = std::min(left.left, right.left);
    merged.top = std::min(left.top, right.top);
    merged.right = std::max(left.right, right.right);
    merged.bottom = std::max(left.bottom, right.bottom);
    return merged;
}

double IntersectionOverUnion(const RECT& left, const RECT& right) {
    const RECT intersection{
        std::max(left.left, right.left),
        std::max(left.top, right.top),
        std::min(left.right, right.right),
        std::min(left.bottom, right.bottom)};

    const double intersectionArea = RectArea(intersection);
    if (intersectionArea <= 0.0) {
        return 0.0;
    }

    const double unionArea = RectArea(left) + RectArea(right) - intersectionArea;
    if (unionArea <= 0.0) {
        return 0.0;
    }

    return intersectionArea / unionArea;
}

bool IsTextLike(UiControlType controlType) {
    return controlType == UiControlType::TextBox ||
        controlType == UiControlType::Document ||
        controlType == UiControlType::MenuItem ||
        controlType == UiControlType::ListItem;
}

std::string VisualKindFromControl(UiControlType controlType) {
    switch (controlType) {
    case UiControlType::Button:
        return "button";
    case UiControlType::TextBox:
        return "text";
    case UiControlType::Menu:
    case UiControlType::MenuItem:
        return "menu";
    case UiControlType::ComboBox:
        return "combo";
    case UiControlType::ListItem:
        return "list";
    case UiControlType::Document:
        return "document";
    case UiControlType::Window:
        return "window";
    case UiControlType::Unknown:
    default:
        return "region";
    }
}

std::uint32_t ColorClusterForElement(const UiElement& element) {
    std::size_t hash = static_cast<std::size_t>(0x811c9dc5u);
    hash = HashCombine(hash, std::hash<std::wstring>{}(element.className));
    hash = HashCombine(hash, std::hash<std::wstring>{}(element.name));
    hash = HashCombine(hash, std::hash<int>{}(static_cast<int>(element.controlType)));
    return static_cast<std::uint32_t>(hash & 0xFFU);
}

std::string BuildVisualId(const UiElement& element, std::size_t index) {
    std::size_t hash = static_cast<std::size_t>(0xcbf29ce484222325ULL);
    hash = HashCombine(hash, std::hash<std::string>{}(element.id));
    hash = HashCombine(hash, std::hash<int>{}(element.bounds.left));
    hash = HashCombine(hash, std::hash<int>{}(element.bounds.top));
    hash = HashCombine(hash, std::hash<int>{}(element.bounds.right));
    hash = HashCombine(hash, std::hash<int>{}(element.bounds.bottom));
    hash = HashCombine(hash, std::hash<std::size_t>{}(index));
    return "v-" + std::to_string(static_cast<std::uint64_t>(hash));
}

std::wstring LabelFromUiElement(const UiElement& element) {
    if (!element.name.empty()) {
        return element.name;
    }
    if (!element.automationId.empty()) {
        return element.automationId;
    }
    if (!element.className.empty()) {
        return element.className;
    }
    return L"";
}

std::string MakeUniqueId(const std::string& baseId, std::unordered_map<std::string, std::size_t>* counts) {
    if (counts == nullptr) {
        return baseId;
    }

    auto it = counts->find(baseId);
    if (it == counts->end()) {
        (*counts)[baseId] = 1U;
        return baseId;
    }

    const std::size_t suffix = it->second;
    ++(it->second);
    return baseId + "-" + std::to_string(suffix);
}

}  // namespace

struct ScreenCaptureEngine::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    DXGI_OUTPUT_DESC outputDesc{};
};

ScreenCaptureEngine::ScreenCaptureEngine()
    : impl_(new Impl()) {}

ScreenCaptureEngine::~ScreenCaptureEngine() {
    Shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool ScreenCaptureEngine::Initialize(std::string* error) {
    if (initialized_) {
        return true;
    }

    initialized_ = true;
    duplicationAvailable_ = false;

    if (impl_ == nullptr) {
        if (error != nullptr) {
            *error = "screen capture implementation unavailable";
        }
        return false;
    }

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT deviceResult = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        impl_->device.ReleaseAndGetAddressOf(),
        &featureLevel,
        impl_->context.ReleaseAndGetAddressOf());

    if (FAILED(deviceResult)) {
        if (error != nullptr) {
            *error = "D3D11 device initialization failed";
        }
        return true;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(impl_->device.As(&dxgiDevice))) {
        if (error != nullptr) {
            *error = "DXGI device query failed";
        }
        return true;
    }

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) {
        if (error != nullptr) {
            *error = "DXGI adapter lookup failed";
        }
        return true;
    }

    ComPtr<IDXGIOutput> output;
    if (FAILED(adapter->EnumOutputs(0, output.GetAddressOf()))) {
        if (error != nullptr) {
            *error = "DXGI output lookup failed";
        }
        return true;
    }

    output->GetDesc(&impl_->outputDesc);

    ComPtr<IDXGIOutput1> output1;
    if (FAILED(output.As(&output1))) {
        if (error != nullptr) {
            *error = "DXGI output1 query failed";
        }
        return true;
    }

    const HRESULT duplicateResult = output1->DuplicateOutput(impl_->device.Get(), impl_->duplication.ReleaseAndGetAddressOf());
    if (FAILED(duplicateResult)) {
        if (error != nullptr) {
            *error = "Desktop duplication unavailable";
        }
        return true;
    }

    duplicationAvailable_ = true;
    return true;
}

void ScreenCaptureEngine::Shutdown() {
    if (impl_ == nullptr) {
        return;
    }

    impl_->duplication.Reset();
    impl_->context.Reset();
    impl_->device.Reset();
    duplicationAvailable_ = false;
    initialized_ = false;
}

bool ScreenCaptureEngine::Capture(ScreenFrame* frame, std::string* error) {
    if (frame == nullptr) {
        if (error != nullptr) {
            *error = "screen frame output is null";
        }
        return false;
    }

    std::string initError;
    if (!initialized_) {
        Initialize(&initError);
    }

    ScreenFrame captured;
    captured.frameId = ++frameCounter_;
    captured.capturedAt = std::chrono::system_clock::now();

    bool usedDesktopDuplication = false;
    if (duplicationAvailable_ && impl_ != nullptr && impl_->duplication != nullptr) {
        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        ComPtr<IDXGIResource> desktopResource;

        const HRESULT acquireResult = impl_->duplication->AcquireNextFrame(0, &frameInfo, desktopResource.GetAddressOf());
        if (acquireResult == S_OK) {
            ComPtr<ID3D11Texture2D> desktopTexture;
            if (SUCCEEDED(desktopResource.As(&desktopTexture)) && desktopTexture != nullptr) {
                D3D11_TEXTURE2D_DESC description{};
                desktopTexture->GetDesc(&description);
                captured.width = static_cast<int>(description.Width);
                captured.height = static_cast<int>(description.Height);
            }

            impl_->duplication->ReleaseFrame();
            usedDesktopDuplication = captured.width > 0 && captured.height > 0;
        } else if (acquireResult == DXGI_ERROR_ACCESS_LOST || acquireResult == DXGI_ERROR_INVALID_CALL) {
            duplicationAvailable_ = false;
        }
    }

    if (!usedDesktopDuplication) {
        const RECT desktopRect = impl_ != nullptr ? impl_->outputDesc.DesktopCoordinates : RECT{0, 0, 0, 0};
        const int desktopWidth = desktopRect.right - desktopRect.left;
        const int desktopHeight = desktopRect.bottom - desktopRect.top;

        captured.width = desktopWidth > 0 ? desktopWidth : std::max(1, GetSystemMetrics(SM_CXSCREEN));
        captured.height = desktopHeight > 0 ? desktopHeight : std::max(1, GetSystemMetrics(SM_CYSCREEN));
        captured.simulated = true;
    }

    captured.width = std::max(1, captured.width);
    captured.height = std::max(1, captured.height);
    captured.valid = true;

    if (!initError.empty() && error != nullptr) {
        *error = initError;
    }

    *frame = captured;
    return true;
}

std::vector<VisualElement> VisualDetector::Detect(const ScreenFrame& frame, const std::vector<UiElement>& uiElements) {
    std::vector<VisualElement> detections;
    if (!frame.valid || frame.width <= 0 || frame.height <= 0) {
        return detections;
    }

    const double frameArea = std::max(1.0, static_cast<double>(frame.width) * static_cast<double>(frame.height));
    detections.reserve(std::min<std::size_t>(uiElements.size() + 9U, 96U));

    std::array<std::size_t, 9> regionCounts{};
    std::array<double, 9> regionOccupancy{};

    const int regionWidth = std::max(1, frame.width / 3);
    const int regionHeight = std::max(1, frame.height / 3);

    for (std::size_t index = 0; index < uiElements.size() && detections.size() < 96U; ++index) {
        const UiElement& element = uiElements[index];
        if (element.isOffscreen) {
            continue;
        }

        const RECT clamped = ClampRectToFrame(element.bounds, frame.width, frame.height);
        const double area = RectArea(clamped);
        if (area <= 1.0) {
            continue;
        }

        const int width = std::max<int>(1, static_cast<int>(clamped.right - clamped.left));
        const int height = std::max<int>(1, static_cast<int>(clamped.bottom - clamped.top));
        const double normalizedArea = std::clamp(area / frameArea, 0.0, 1.0);

        const int centerX = clamped.left + (width / 2);
        const int centerY = clamped.top + (height / 2);
        const int regionX = std::clamp(centerX / regionWidth, 0, 2);
        const int regionY = std::clamp(centerY / regionHeight, 0, 2);
        const std::size_t regionIndex = static_cast<std::size_t>((regionY * 3) + regionX);

        ++regionCounts[regionIndex];
        regionOccupancy[regionIndex] += normalizedArea;

        VisualElement visual;
        visual.id = BuildVisualId(element, index);
        visual.kind = VisualKindFromControl(element.controlType);
        visual.bounds = clamped;
        visual.colorCluster = ColorClusterForElement(element);
        visual.edgeDensity = std::clamp(
            (2.0 * static_cast<double>(width + height)) / std::max(1.0, area),
            0.0,
            1.0);
        visual.textLike = IsTextLike(element.controlType) || !element.value.empty();

        const double focusBias = element.isFocused ? 0.18 : 0.0;
        const double textBias = visual.textLike ? 0.10 : 0.0;
        visual.confidence = std::clamp(0.50 + (normalizedArea * 0.60) + focusBias + textBias, 0.05, 0.99);

        detections.push_back(std::move(visual));
    }

    for (int row = 0; row < 3 && detections.size() < 96U; ++row) {
        for (int col = 0; col < 3 && detections.size() < 96U; ++col) {
            const std::size_t regionIndex = static_cast<std::size_t>((row * 3) + col);
            if (regionCounts[regionIndex] == 0U) {
                continue;
            }

            VisualElement segment;
            segment.id = "seg-" + std::to_string(regionIndex);
            segment.kind = "segment";
            segment.bounds.left = col * regionWidth;
            segment.bounds.top = row * regionHeight;
            segment.bounds.right = (col == 2) ? frame.width : ((col + 1) * regionWidth);
            segment.bounds.bottom = (row == 2) ? frame.height : ((row + 1) * regionHeight);
            segment.colorCluster = static_cast<std::uint32_t>((regionIndex * 29U) & 0xFFU);
            segment.edgeDensity = std::clamp(regionOccupancy[regionIndex], 0.0, 1.0);
            segment.textLike = false;
            segment.confidence = std::clamp(0.25 + (regionOccupancy[regionIndex] * 0.75), 0.05, 0.90);
            detections.push_back(std::move(segment));
        }
    }

    return detections;
}

ScreenState ScreenStateAssembler::Build(
    std::uint64_t environmentSequence,
    std::chrono::system_clock::time_point capturedAt,
    POINT cursorPosition,
    const std::vector<UiElement>& uiElements,
    const ScreenFrame& frame,
    const std::vector<VisualElement>& visualElements) {
    ScreenState state;
    state.frameId = frame.frameId;
    state.environmentSequence = environmentSequence;
    state.capturedAt = frame.capturedAt.time_since_epoch().count() == 0 ? capturedAt : frame.capturedAt;
    state.cursorPosition = cursorPosition;
    state.width = frame.width;
    state.height = frame.height;
    state.simulated = frame.simulated;
    state.valid = frame.valid;
    state.visualElements = visualElements;

    std::unordered_map<std::string, std::size_t> idCounts;
    idCounts.reserve(uiElements.size() + visualElements.size() + 4U);

    state.elements.reserve(uiElements.size() + visualElements.size() + 1U);

    for (std::size_t index = 0; index < uiElements.size(); ++index) {
        const UiElement& uiElement = uiElements[index];
        if (uiElement.isOffscreen) {
            continue;
        }

        ScreenElement element;
        element.source = "uia";
        element.uiElementId = uiElement.id;
        element.label = LabelFromUiElement(uiElement);
        element.bounds = ClampRectToFrame(uiElement.bounds, std::max(1, frame.width), std::max(1, frame.height));
        element.focused = uiElement.isFocused;
        element.textLike = IsTextLike(uiElement.controlType) || !uiElement.value.empty();
        element.confidence = uiElement.isFocused ? 0.96 : 0.84;

        std::string baseId;
        if (!uiElement.id.empty()) {
            baseId = "uia:" + uiElement.id;
        } else {
            const std::size_t hash = HashCombine(
                std::hash<std::wstring>{}(element.label),
                std::hash<std::size_t>{}(index));
            baseId = "uia:auto-" + std::to_string(static_cast<std::uint64_t>(hash));
        }
        element.id = MakeUniqueId(baseId, &idCounts);

        state.elements.push_back(std::move(element));
    }

    for (const VisualElement& visual : visualElements) {
        const RECT visualBounds = ClampRectToFrame(visual.bounds, std::max(1, frame.width), std::max(1, frame.height));

        std::size_t bestIndex = std::numeric_limits<std::size_t>::max();
        double bestOverlap = 0.0;

        for (std::size_t index = 0; index < state.elements.size(); ++index) {
            const ScreenElement& candidate = state.elements[index];
            if (candidate.source == "cursor") {
                continue;
            }

            const double overlap = IntersectionOverUnion(candidate.bounds, visualBounds);
            if (overlap > bestOverlap) {
                bestOverlap = overlap;
                bestIndex = index;
            }
        }

        const double threshold = visual.textLike ? 0.35 : 0.45;
        if (bestIndex != std::numeric_limits<std::size_t>::max() && bestOverlap >= threshold) {
            ScreenElement& merged = state.elements[bestIndex];
            if (merged.source == "uia") {
                merged.source = "merged";
            }
            merged.bounds = UnionRects(merged.bounds, visualBounds);
            merged.confidence = std::max(merged.confidence, visual.confidence);
            merged.textLike = merged.textLike || visual.textLike;
            continue;
        }

        ScreenElement element;
        element.source = "vision";
        element.uiElementId.clear();
        element.label.assign(visual.kind.begin(), visual.kind.end());
        element.bounds = visualBounds;
        element.confidence = visual.confidence;
        element.focused = false;
        element.textLike = visual.textLike;
        element.id = MakeUniqueId("vision:" + visual.id, &idCounts);

        state.elements.push_back(std::move(element));
    }

    ScreenElement cursor;
    cursor.source = "cursor";
    cursor.uiElementId.clear();
    cursor.label = L"cursor";
    const int cursorX = static_cast<int>(cursorPosition.x);
    const int cursorY = static_cast<int>(cursorPosition.y);
    const int cursorLeft = std::clamp(cursorX - 2, 0, std::max(0, frame.width - 1));
    const int cursorTop = std::clamp(cursorY - 2, 0, std::max(0, frame.height - 1));
    const int cursorRight = std::clamp(cursorX + 3, cursorLeft + 1, std::max(cursorLeft + 1, frame.width));
    const int cursorBottom = std::clamp(cursorY + 3, cursorTop + 1, std::max(cursorTop + 1, frame.height));
    cursor.bounds.left = static_cast<LONG>(cursorLeft);
    cursor.bounds.top = static_cast<LONG>(cursorTop);
    cursor.bounds.right = static_cast<LONG>(cursorRight);
    cursor.bounds.bottom = static_cast<LONG>(cursorBottom);
    cursor.confidence = 1.0;
    cursor.focused = true;
    cursor.textLike = false;
    cursor.id = MakeUniqueId(
        "cursor:" + std::to_string(cursorPosition.x) + ":" + std::to_string(cursorPosition.y),
        &idCounts);

    state.elements.push_back(std::move(cursor));

    std::size_t signature = static_cast<std::size_t>(0xcbf29ce484222325ULL);
    signature = HashCombine(signature, std::hash<std::uint64_t>{}(state.frameId));
    signature = HashCombine(signature, std::hash<int>{}(state.width));
    signature = HashCombine(signature, std::hash<int>{}(state.height));

    for (const ScreenElement& element : state.elements) {
        signature = HashCombine(signature, std::hash<std::string>{}(element.id));
        signature = HashCombine(signature, std::hash<std::string>{}(element.source));
        signature = HashCombine(signature, std::hash<std::wstring>{}(element.label));
        signature = HashCombine(signature, std::hash<int>{}(element.bounds.left));
        signature = HashCombine(signature, std::hash<int>{}(element.bounds.top));
        signature = HashCombine(signature, std::hash<int>{}(element.bounds.right));
        signature = HashCombine(signature, std::hash<int>{}(element.bounds.bottom));
    }

    state.signature = static_cast<std::uint64_t>(signature == 0 ? 1 : signature);
    return state;
}

}  // namespace iee
