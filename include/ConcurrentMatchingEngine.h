// include/ConcurrentMatchingEngine.h
#ifndef CONCURRENTMATCHINGENGINE_H
#define CONCURRENTMATCHINGENGINE_H

#include "MatchingEngine.h"
#include "RingBuffer.h"
#include <thread>
#include <atomic>
#include <vector>

// Multi-threaded matching engine using ring buffer.
// Producer threads push orders, consumer thread matches them.
class ConcurrentMatchingEngine {
public:
    // buffer_size must be power of 2
    explicit ConcurrentMatchingEngine(size_t buffer_size = 1024);
    ~ConcurrentMatchingEngine();

    // Start consumer thread (matching engine)
    void start();

    // Stop consumer thread
    void stop();

    // Producer interface: push order for processing
    bool submitOrder(const Order& order);

    // Get number of processed orders (for statistics)
    size_t getProcessedCount() const;

    // Get current order book state
    const MatchingEngine& getEngine() const { return engine_; }

private:
    void consumerLoop();  // runs in separate thread

    RingBuffer buffer_;
    MatchingEngine engine_;
    std::thread consumer_thread_;
    std::atomic<bool> running_;
    std::atomic<size_t> processed_count_;
};

#endif // CONCURRENTMATCHINGENGINE_H