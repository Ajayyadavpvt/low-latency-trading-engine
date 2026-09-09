#include "MarketDataPublisher.h"
#include <atomic>
#include <memory>
#include <mutex>

MarketDataPublisher::MarketDataPublisher()
    : subscribers_(std::make_shared<const std::vector<MarketDataSubscriber*>>())
{}

void MarketDataPublisher::reserve(std::size_t n) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto current = std::atomic_load(&subscribers_);
    auto new_list = std::make_shared<std::vector<MarketDataSubscriber*>>();
    new_list->reserve(n);
    *new_list = *current;

    std::shared_ptr<const std::vector<MarketDataSubscriber*>> snapshot = new_list;
    std::atomic_store(&subscribers_, snapshot);
}

void MarketDataPublisher::subscribe(MarketDataSubscriber* subscriber) {
    if (!subscriber) return;
    std::lock_guard<std::mutex> lock(mutex_);

    auto current = std::atomic_load(&subscribers_);
    auto new_list = std::make_shared<std::vector<MarketDataSubscriber*>>(*current);

    if (std::find(new_list->begin(), new_list->end(), subscriber) != new_list->end()) {
        return;
    }

    new_list->push_back(subscriber);

    std::shared_ptr<const std::vector<MarketDataSubscriber*>> snapshot = new_list;
    std::atomic_store(&subscribers_, snapshot);
}

void MarketDataPublisher::unsubscribe(MarketDataSubscriber* subscriber) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto current = std::atomic_load(&subscribers_);
    auto new_list = std::make_shared<std::vector<MarketDataSubscriber*>>(*current);

    new_list->erase(
        std::remove(new_list->begin(), new_list->end(), subscriber),
        new_list->end());

    std::shared_ptr<const std::vector<MarketDataSubscriber*>> snapshot = new_list;
    std::atomic_store(&subscribers_, snapshot);
}

void MarketDataPublisher::publish(const MarketEvent& event) const noexcept {
    auto snapshot = std::atomic_load(&subscribers_);

    for (auto* subscriber : *snapshot) {
        subscriber->onEvent(event);
    }
}