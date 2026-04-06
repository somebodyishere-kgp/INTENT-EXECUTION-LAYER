#pragma once

#include <Windows.h>

#include <atomic>
#include <string>
#include <thread>

#include "EventBus.h"

namespace iee {

class UiChangeWatcher {
public:
    explicit UiChangeWatcher(EventBus& eventBus);
    ~UiChangeWatcher();

    void Start();
    void Stop();

private:
    static void CALLBACK WinEventCallback(
        HWINEVENTHOOK hook,
        DWORD event,
        HWND hwnd,
        LONG idObject,
        LONG idChild,
        DWORD eventThread,
        DWORD eventTime);

    void PublishUiEvent(const std::string& message, EventPriority priority);

    EventBus& eventBus_;
    HWINEVENTHOOK foregroundHook_{nullptr};
    HWINEVENTHOOK focusHook_{nullptr};
    HWINEVENTHOOK nameChangeHook_{nullptr};
    std::atomic<bool> running_{false};

    static UiChangeWatcher* instance_;
};

class FileSystemWatcher {
public:
    FileSystemWatcher(const std::wstring& rootPath, EventBus& eventBus);
    ~FileSystemWatcher();

    void Start();
    void Stop();

private:
    void WatchLoop();

    std::wstring rootPath_;
    EventBus& eventBus_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    HANDLE directoryHandle_{INVALID_HANDLE_VALUE};
};

}  // namespace iee
