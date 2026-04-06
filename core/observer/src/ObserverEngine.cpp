#include "ObserverEngine.h"

#include <atomic>
#include <filesystem>
#include <system_error>

#include "Logger.h"

namespace iee {
namespace {

std::atomic<std::uint64_t> g_snapshotSequence{1};

std::uint64_t ToFileTicks(std::filesystem::file_time_type fileTime) {
    const auto duration = fileTime.time_since_epoch();
    return static_cast<std::uint64_t>(duration.count());
}

}  // namespace

ObserverEngine::ObserverEngine(IAccessibilityLayer& accessibilityLayer)
    : accessibilityLayer_(accessibilityLayer) {}

ObserverSnapshot ObserverEngine::Capture() {
    ObserverSnapshot snapshot;
    snapshot.capturedAt = std::chrono::system_clock::now();
    snapshot.sequence = g_snapshotSequence.fetch_add(1, std::memory_order_relaxed);

    snapshot.activeWindow = GetForegroundWindow();
    if (!snapshot.activeWindow) {
        Logger::Warning("ObserverEngine", "No foreground window detected");
        return snapshot;
    }

    wchar_t titleBuffer[512] = {};
    GetWindowTextW(snapshot.activeWindow, titleBuffer, static_cast<int>(std::size(titleBuffer)));
    snapshot.activeWindowTitle = titleBuffer;

    DWORD processId = 0;
    GetWindowThreadProcessId(snapshot.activeWindow, &processId);
    snapshot.activeProcessId = processId;
    snapshot.activeProcessPath = GetProcessPath(processId);

    POINT cursor{};
    if (GetCursorPos(&cursor)) {
        snapshot.cursorPosition = cursor;
    }

    snapshot.uiElements = accessibilityLayer_.CaptureTree(snapshot.activeWindow);
    snapshot.fileSystemEntries = CaptureFileSystemSnapshot();
    snapshot.valid = true;

    Logger::Info("ObserverEngine", "Captured observer snapshot");
    return snapshot;
}

std::wstring ObserverEngine::GetProcessPath(DWORD processId) const {
    if (processId == 0) {
        return L"";
    }

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!processHandle) {
        return L"";
    }

    std::wstring path(32768, L'\0');
    DWORD pathSize = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(processHandle, 0, path.data(), &pathSize)) {
        CloseHandle(processHandle);
        return L"";
    }

    CloseHandle(processHandle);
    path.resize(pathSize);
    return path;
}

std::vector<FileSystemEntry> ObserverEngine::CaptureFileSystemSnapshot() const {
    std::vector<FileSystemEntry> entries;
    std::error_code ec;

    const auto currentDirectory = std::filesystem::current_path(ec);
    if (ec) {
        Logger::Warning("ObserverEngine", "Failed to read current directory");
        return entries;
    }

    constexpr std::size_t kMaxEntries = 256;
    std::size_t count = 0;

    for (const auto& dirEntry : std::filesystem::directory_iterator(currentDirectory, ec)) {
        if (ec) {
            break;
        }
        if (count++ >= kMaxEntries) {
            break;
        }

        FileSystemEntry entry;
        entry.path = dirEntry.path().wstring();
        entry.isDirectory = dirEntry.is_directory(ec);
        if (ec) {
            ec.clear();
            entry.isDirectory = false;
        }

        if (!entry.isDirectory) {
            entry.size = dirEntry.file_size(ec);
            if (ec) {
                ec.clear();
                entry.size = 0;
            }
        }

        const auto writeTime = dirEntry.last_write_time(ec);
        if (!ec) {
            entry.lastWriteTicks = ToFileTicks(writeTime);
        } else {
            ec.clear();
        }

        entries.push_back(std::move(entry));
    }

    return entries;
}

}  // namespace iee
