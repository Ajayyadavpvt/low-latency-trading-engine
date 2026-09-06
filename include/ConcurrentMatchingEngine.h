// include/ConcurrentMatchingEngine.h
#ifndef CONCURRENTMATCHINGENGINE_H
#define CONCURRENTMATCHINGENGINE_H

#include "MatchingEngine.h"
#include "RingBuffer.h"
#include "Order.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <cstddef>
#include <cassert>

enum class BackpressurePolicy {
    REJECT,   // default: return false when buffer is full
    BLOCK,    // wait until space available (SPSC only), false if stopped while waiting
    DROP      // silently discard when full (counter available via getDroppedCount)
};

class ConcurrentMatchingEngine {
public:
    explicit ConcurrentMatchingEngine(size_t buffer_size = 1024);
    ~ConcurrentMatchingEngine();

    ConcurrentMatchingEngine(const ConcurrentMatchingEngine&) = delete;
    ConcurrentMatchingEngine& operator=(const ConcurrentMatchingEngine&) = delete;

    void start();
    void stop();

    bool submitOrder(const Order& order);

    void setBackpressurePolicy(BackpressurePolicy policy) {
        assert(!running_.load(std::memory_order_relaxed) &&
               "Cannot change backpressure policy while running");
        policy_.store(policy, std::memory_order_relaxed);
    }

    BackpressurePolicy getBackpressurePolicy() const {
        return policy_.load(std::memory_order_relaxed);
    }

    size_t getProcessedCount() const;
    size_t getDroppedCount() const { return dropped_count_.load(std::memory_order_relaxed); }

    const MatchingEngine& getEngine() const { return engine_; }

private:
    void consumerLoop();

    RingBuffer buffer_;
    MatchingEngine engine_;
    std::thread consumer_thread_;
    std::mutex start_stop_mutex_;

    std::atomic<bool> running_{false};
    std::atomic<size_t> processed_count_{0};
    std::atomic<size_t> dropped_count_{0};
    std::atomic<BackpressurePolicy> policy_{BackpressurePolicy::REJECT};
};

#endif // CONCURRENTMATCHINGENGINE_H