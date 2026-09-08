#pragma once
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>
#include "MarketDataTypes.h"

class MarketDataSubscriber {
public:
    virtual ~MarketDataSubscriber() = default;
    // Must not throw; implementations should handle errors internally
    virtual void onEvent(const MarketEvent& event) noexcept = 0;
};

class MarketDataPublisher {
public:
    MarketDataPublisher();

    // Thread-safe modifications (copy-on-write)
    void subscribe(MarketDataSubscriber* subscriber);
    void unsubscribe(MarketDataSubscriber* subscriber);

    // Hot path: lock-free, wait-free read
    void publish(const MarketEvent& event) const noexcept;

    // Optional: preallocate space for expected number of subscribers
    void reserve(std::size_t n);

private:
    // Copy-on-write snapshot of subscribers
    // shared_ptr to immutable vector ensures safe concurrent access
    std::shared_ptr<const std::vector<MarketDataSubscriber*>> subscribers_;
    std::mutex mutex_; // only protects modifications (subscribe/unsubscribe)
};