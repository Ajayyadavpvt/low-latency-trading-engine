#include "MarketDataPublisher.h"
#include <atomic>

MarketDataPublisher::MarketDataPublisher()
    : subscribers_(std::make_shared<const std::vector<MarketDataSubscriber*>>())
{}

void MarketDataPublisher::reserve(std::size_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto new_list = std::make_shared<std::vector<MarketDataSubscriber*>>();
    new_list->reserve(n);
    *new_list = *subscribers_;  // copy existing (if any)
    subscribers_ = std::move(new_list);
}

void MarketDataPublisher::subscribe(MarketDataSubscriber* subscriber) {
    if (!subscriber) return;
    std::lock_guard<std::mutex> lock(mutex_);

    // Create a copy of current subscriber list
    auto new_list = std::make_shared<std::vector<MarketDataSubscriber*>>(*subscribers_);

    // Avoid duplicates
    if (std::find(new_list->begin(), new_list->end(), subscriber) != new_list->end()) {
        return;
    }

    new_list->push_back(subscriber);
    subscribers_ = std::move(new_list);
}

void MarketDataPublisher::unsubscribe(MarketDataSubscriber* subscriber) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto new_list = std::make_shared<std::vector<MarketDataSubscriber*>>(*subscribers_);
    new_list->erase(std::remove(new_list->begin(), new_list->end(), subscriber),
                    new_list->end());
    subscribers_ = std::move(new_list);
}

void MarketDataPublisher::publish(const MarketEvent& event) const noexcept {
    // Atomically acquire the current snapshot (lock-free for readers)
    auto snapshot = std::atomic_load(&subscribers_);

    for (auto* subscriber : *snapshot) {
        subscriber->onEvent(event);
    }
}