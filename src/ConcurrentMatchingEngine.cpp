// src/ConcurrentMatchingEngine.cpp
#include "../include/ConcurrentMatchingEngine.h"
#include <chrono>
#include <thread>      // for std::this_thread
#include <exception>

ConcurrentMatchingEngine::ConcurrentMatchingEngine(size_t buffer_size)
    : buffer_(buffer_size)
    , running_(false)
    , processed_count_(0)
{
}

ConcurrentMatchingEngine::~ConcurrentMatchingEngine() {
    stop();
}

void ConcurrentMatchingEngine::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // already running or another thread started it
    }
    consumer_thread_ = std::thread(&ConcurrentMatchingEngine::consumerLoop, this);
}

void ConcurrentMatchingEngine::stop() {
    // Prevent self-join deadlock
    if (consumer_thread_.joinable() &&
        std::this_thread::get_id() == consumer_thread_.get_id()) {
        running_.store(false);
        return; // cannot join self; consumer will exit on next loop check
    }

    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return; // already stopped
    }

    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }
}

bool ConcurrentMatchingEngine::submitOrder(const Order& order) {
    // Caller must ensure no submitOrder calls happen after stop() is invoked.
    return buffer_.push(order);
}

size_t ConcurrentMatchingEngine::getProcessedCount() const {
    return processed_count_.load(std::memory_order_relaxed);
}

void ConcurrentMatchingEngine::consumerLoop() {
    while (running_.load() || !buffer_.isEmpty()) {
        Order order; // default placeholder, validation-free
        if (buffer_.pop(order)) {
            try {
                engine_.processOrder(order);
                processed_count_.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                // Log error in production; for now, swallow and continue
                // TODO Phase 11: Add proper error handling/logging
            }
        } else {
            // Hybrid busy-spin + sleep: Phase 8 optimization
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}