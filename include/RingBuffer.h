// include/RingBuffer.h
#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <vector>
#include <atomic>
#include <cstddef>
#include "Order.h"

// Lock-free Single Producer Single Consumer (SPSC) ring buffer.
// One thread pushes orders, another thread pops them.
// No locks used - relies on atomic operations and memory ordering.
//
// IMPORTANT: This queue is strictly SPSC. Do NOT call push() from more
// than one thread, or pop() from more than one thread.
//
// size() / isEmpty() / isFull() are advisory snapshots — they can be
// stale the moment they're read, by design in a lock-free structure.
class RingBuffer {
public:
    // Constructor: size must be power of 2 for efficient modulo.
    explicit RingBuffer(size_t size);

    // Push an order into the buffer (producer thread only).
    // Returns true if successful, false if buffer is full.
    bool push(const Order& order);

    // Pop an order from the buffer (consumer thread only).
    // Returns true if successful, false if buffer is empty.
    bool pop(Order& order);

    bool isEmpty() const;
    bool isFull() const;
    size_t size() const;

private:
    static constexpr size_t kCacheLineSize = 64;

    std::vector<Order> buffer_;   // Fixed-size storage

    // Separate cache lines to avoid false sharing between producer
    // (tail_) and consumer (head_) threads.
    alignas(kCacheLineSize) std::atomic<size_t> tail_;  // producer only
    alignas(kCacheLineSize) std::atomic<size_t> head_;  // consumer only

    size_t capacity_;
    size_t mask_;
};

#endif // RINGBUFFER_H