#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace iee {

struct EnumClassHash {
    template <typename T>
    std::size_t operator()(T value) const noexcept {
        return static_cast<std::size_t>(value);
    }
};

enum class EventType {
    SnapshotCaptured,
    RegistryUpdated,
    ExecutionStarted,
    ExecutionRetry,
    ExecutionFallback,
    ExecutionFinished,
    UiChanged,
    FileSystemChanged,
    AmbiguityDetected,
    Error
};

enum class EventPriority {
    HIGH,
    MEDIUM,
    LOW
};

struct Event {
    EventType type{EventType::Error};
    std::string source;
    std::string message;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    EventPriority priority{EventPriority::MEDIUM};
};

using EventHandler = std::function<void(const Event&)>;

class EventBus {
public:
    using SubscriptionId = std::uint64_t;

    SubscriptionId Subscribe(EventType type, EventHandler handler);
    void Unsubscribe(EventType type, SubscriptionId id);
    void Publish(Event event);

private:
    using Subscription = std::pair<SubscriptionId, EventHandler>;

    mutable std::mutex mutex_;
    std::unordered_map<EventType, std::vector<Subscription>, EnumClassHash> subscriptions_;
    SubscriptionId nextSubscriptionId_{1};
};

}  // namespace iee
