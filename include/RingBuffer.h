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
class RingBuffer {
public:
    // Constructor: size must be power of 2 for efficient modulo operation.
    explicit RingBuffer(size_t size);

    // Push an order into the buffer (called by producer thread).
    // Returns true if successful, false if buffer is full.
    bool push(const Order& order);

    // Pop an order from the buffer (called by consumer thread).
    // Returns true if successful, false if buffer is empty.
    bool pop(Order& order);

    // Check if buffer is empty (consumer side).
    bool isEmpty() const;

    // Check if buffer is full (producer side).
    bool isFull() const;

    // Current number of elements in the buffer.
    size_t size() const;

private:
    std::vector<Order> buffer_;   // Fixed-size storage
    std::atomic<size_t> head_;    // Consumer index (next pop position)
    std::atomic<size_t> tail_;    // Producer index (next push position)
    size_t capacity_;             // Total capacity (power of 2)
    size_t mask_;                 // capacity_ - 1 for fast modulo
};

#endif // RINGBUFFER_H