// src/RingBuffer.cpp
#include "../include/RingBuffer.h"
#include <thread>
#include <cassert>

RingBuffer::RingBuffer(size_t size) 
    : buffer_(size)
    , head_(0)
    , tail_(0)
    , capacity_(size)
    , mask_(size - 1)
{
    // Ensure size is power of 2 for efficient modulo
    // (size & (size - 1)) == 0 checks power of 2
    assert((size & (size - 1)) == 0 && "Size must be power of 2");
}

bool RingBuffer::push(const Order& order) {
    size_t current_tail = tail_.load(std::memory_order_relaxed);
    size_t next_tail = (current_tail + 1) & mask_;
    
    // Check if full: next_tail == head means buffer is full
    if (next_tail == head_.load(std::memory_order_acquire)) {
        return false; // Buffer full
    }
    
    buffer_[current_tail] = order;
    tail_.store(next_tail, std::memory_order_release);
    return true;
}

bool RingBuffer::pop(Order& order) {
    size_t current_head = head_.load(std::memory_order_relaxed);
    
    // Check if empty: head == tail means buffer is empty
    if (current_head == tail_.load(std::memory_order_acquire)) {
        return false; // Buffer empty
    }
    
    order = buffer_[current_head];
    head_.store((current_head + 1) & mask_, std::memory_order_release);
    return true;
}

bool RingBuffer::isEmpty() const {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}

bool RingBuffer::isFull() const {
    size_t next_tail = (tail_.load(std::memory_order_acquire) + 1) & mask_;
    return next_tail == head_.load(std::memory_order_acquire);
}

size_t RingBuffer::size() const {
    size_t tail = tail_.load(std::memory_order_acquire);
    size_t head = head_.load(std::memory_order_acquire);
    if (tail >= head) {
        return tail - head;
    } else {
        return capacity_ - head + tail;
    }
}