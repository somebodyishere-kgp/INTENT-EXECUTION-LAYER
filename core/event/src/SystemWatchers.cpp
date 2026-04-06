#include "SystemWatchers.h"

#include <vector>

namespace iee {

UiChangeWatcher* UiChangeWatcher::instance_ = nullptr;

UiChangeWatcher::UiChangeWatcher(EventBus& eventBus)
    : eventBus_(eventBus) {}

UiChangeWatcher::~UiChangeWatcher() {
    Stop();
}

void UiChangeWatcher::Start() {
    if (running_.exchange(true)) {
        return;
    }

    instance_ = this;

    foregroundHook_ = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        nullptr,
        &UiChangeWatcher::WinEventCallback,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    focusHook_ = SetWinEventHook(
        EVENT_OBJECT_FOCUS,
        EVENT_OBJECT_FOCUS,
        nullptr,
        &UiChangeWatcher::WinEventCallback,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    nameChangeHook_ = SetWinEventHook(
        EVENT_OBJECT_NAMECHANGE,
        EVENT_OBJECT_NAMECHANGE,
        nullptr,
        &UiChangeWatcher::WinEventCallback,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void UiChangeWatcher::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (foregroundHook_ != nullptr) {
        UnhookWinEvent(foregroundHook_);
        foregroundHook_ = nullptr;
    }

    if (focusHook_ != nullptr) {
        UnhookWinEvent(focusHook_);
        focusHook_ = nullptr;
    }

    if (nameChangeHook_ != nullptr) {
        UnhookWinEvent(nameChangeHook_);
        nameChangeHook_ = nullptr;
    }

    instance_ = nullptr;
}

void CALLBACK UiChangeWatcher::WinEventCallback(
    HWINEVENTHOOK,
    DWORD event,
    HWND,
    LONG,
    LONG,
    DWORD,
    DWORD) {
    if (instance_ == nullptr || !instance_->running_.load()) {
        return;
    }

    switch (event) {
    case EVENT_SYSTEM_FOREGROUND:
        instance_->PublishUiEvent("Foreground window changed", EventPriority::HIGH);
        break;
    case EVENT_OBJECT_FOCUS:
        instance_->PublishUiEvent("UI focus changed", EventPriority::HIGH);
        break;
    case EVENT_OBJECT_NAMECHANGE:
        instance_->PublishUiEvent("UI element name changed", EventPriority::MEDIUM);
        break;
    default:
        break;
    }
}

void UiChangeWatcher::PublishUiEvent(const std::string& message, EventPriority priority) {
    eventBus_.Publish(Event{EventType::UiChanged, "UiChangeWatcher", message, std::chrono::system_clock::now(), priority});
}

FileSystemWatcher::FileSystemWatcher(const std::wstring& rootPath, EventBus& eventBus)
    : rootPath_(rootPath), eventBus_(eventBus) {}

FileSystemWatcher::~FileSystemWatcher() {
    Stop();
}

void FileSystemWatcher::Start() {
    if (running_.exchange(true)) {
        return;
    }

    worker_ = std::thread(&FileSystemWatcher::WatchLoop, this);
}

void FileSystemWatcher::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (directoryHandle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(directoryHandle_, nullptr);
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    if (directoryHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(directoryHandle_);
        directoryHandle_ = INVALID_HANDLE_VALUE;
    }
}

void FileSystemWatcher::WatchLoop() {
    directoryHandle_ = CreateFileW(
        rootPath_.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (directoryHandle_ == INVALID_HANDLE_VALUE) {
        eventBus_.Publish(Event{EventType::Error, "FileSystemWatcher", "Unable to open directory watch handle", std::chrono::system_clock::now(), EventPriority::HIGH});
        return;
    }

    std::vector<std::byte> buffer(8192);

    while (running_.load()) {
        DWORD bytesReturned = 0;
        const BOOL ok = ReadDirectoryChangesW(
            directoryHandle_,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME |
                FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytesReturned,
            nullptr,
            nullptr);

        if (!ok) {
            if (running_.load()) {
                eventBus_.Publish(Event{EventType::Error, "FileSystemWatcher", "ReadDirectoryChangesW failed", std::chrono::system_clock::now(), EventPriority::HIGH});
            }
            continue;
        }

        if (bytesReturned > 0) {
            eventBus_.Publish(Event{EventType::FileSystemChanged, "FileSystemWatcher", "Filesystem change detected", std::chrono::system_clock::now(), EventPriority::LOW});
        }
    }
}

}  // namespace iee
