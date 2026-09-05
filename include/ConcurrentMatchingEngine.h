// include/ConcurrentMatchingEngine.h
#ifndef CONCURRENTMATCHINGENGINE_H
#define CONCURRENTMATCHINGENGINE_H

#include "MatchingEngine.h"
#include "RingBuffer.h"
#include <thread>
#include <atomic>
#include <cstddef>

// Multi-threaded matching engine using SPSC ring buffer.
//
// CONCURRENCY MODEL:
// - Single producer thread calls submitOrder().
// - Single consumer thread (started by start()) pops orders and matches them.
// - This is intentionally SPSC — do NOT call submitOrder() from multiple threads.
//
// THREAD-SAFETY CONTRACT:
// - getEngine() is only safe to call AFTER stop() has returned.
//   While the engine is running, the consumer thread owns MatchingEngine
//   exclusively, so any concurrent read/write is a data race.
class ConcurrentMatchingEngine {
public:
    // buffer_size must be a non-zero power of 2 (enforced by RingBuffer).
    explicit ConcurrentMatchingEngine(size_t buffer_size = 1024);

    // Destructor — safely stops the consumer thread if still running.
    ~ConcurrentMatchingEngine();

    // Copy/move deleted because of std::thread and atomics.
    ConcurrentMatchingEngine(const ConcurrentMatchingEngine&) = delete;
    ConcurrentMatchingEngine& operator=(const ConcurrentMatchingEngine&) = delete;

    // Start the consumer thread. Calling start() twice is a no-op.
    void start();

    // Stop the consumer thread.
    // Blocks until all already-submitted orders have been processed (drain),
    // then joins the consumer thread. Safe to call multiple times.
    void stop();

    // Producer interface: submit an order for processing.
    // Returns false if the ring buffer is full (caller should retry later).
    // Only safe to call from a single producer thread.
    bool submitOrder(const Order& order);

    // Number of orders processed so far (approximate if running; exact after stop()).
    size_t getProcessedCount() const;

    // Access to the underlying matching engine.
    // WARNING: Only call this AFTER stop() has returned. See class comment.
    const MatchingEngine& getEngine() const { return engine_; }

private:
    void consumerLoop();  // runs in separate thread

    RingBuffer buffer_;
    MatchingEngine engine_;
    std::thread consumer_thread_;

    // Default member initializers fix uninitialized atomic issue (pre-C++20).
    std::atomic<bool> running_{false};
    std::atomic<size_t> processed_count_{0};
};

#endif // CONCURRENTMATCHINGENGINE_H