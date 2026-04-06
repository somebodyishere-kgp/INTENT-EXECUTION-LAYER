#include "EventBus.h"

#include <algorithm>
#include <exception>

#include "Logger.h"

namespace iee {

EventBus::SubscriptionId EventBus::Subscribe(EventType type, EventHandler handler) {
    if (!handler) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const SubscriptionId id = nextSubscriptionId_++;
    subscriptions_[type].push_back({id, std::move(handler)});
    return id;
}

void EventBus::Unsubscribe(EventType type, SubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(type);
    if (it == subscriptions_.end()) {
        return;
    }

    auto& handlers = it->second;
    handlers.erase(
        std::remove_if(
            handlers.begin(),
            handlers.end(),
            [id](const Subscription& subscription) { return subscription.first == id; }),
        handlers.end());
}

void EventBus::Publish(Event event) {
    std::vector<Subscription> handlers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = subscriptions_.find(event.type);
        if (it != subscriptions_.end()) {
            handlers = it->second;
        }
    }

    if (event.timestamp.time_since_epoch().count() == 0) {
        event.timestamp = std::chrono::system_clock::now();
    }

    for (const auto& subscription : handlers) {
        try {
            subscription.second(event);
        } catch (const std::exception& ex) {
            Logger::Error("EventBus", std::string("Event handler failed: ") + ex.what());
        } catch (...) {
            Logger::Error("EventBus", "Event handler failed with unknown exception");
        }
    }
}

}  // namespace iee
