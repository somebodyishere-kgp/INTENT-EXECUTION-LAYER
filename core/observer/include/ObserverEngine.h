#pragma once

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "AccessibilityLayer.h"
#include "UiElement.h"

namespace iee {

struct FileSystemEntry {
    std::wstring path;
    bool isDirectory{false};
    std::uintmax_t size{0};
    std::uint64_t lastWriteTicks{0};
};

struct ObserverSnapshot {
    std::chrono::system_clock::time_point capturedAt{std::chrono::system_clock::now()};
    std::uint64_t sequence{0};
    HWND activeWindow{nullptr};
    std::wstring activeWindowTitle;
    DWORD activeProcessId{0};
    std::wstring activeProcessPath;
    POINT cursorPosition{0, 0};
    std::vector<UiElement> uiElements;
    std::vector<FileSystemEntry> fileSystemEntries;
    bool valid{false};
};

class IObserverEngine {
public:
    virtual ~IObserverEngine() = default;
    virtual ObserverSnapshot Capture() = 0;
};

class ObserverEngine : public IObserverEngine {
public:
    explicit ObserverEngine(IAccessibilityLayer& accessibilityLayer);

    ObserverSnapshot Capture() override;

private:
    std::wstring GetProcessPath(DWORD processId) const;
    std::vector<FileSystemEntry> CaptureFileSystemSnapshot() const;

    IAccessibilityLayer& accessibilityLayer_;
};

}  // namespace iee
