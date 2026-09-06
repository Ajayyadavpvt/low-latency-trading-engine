// src/ConcurrentMatchingEngine.cpp
#include "../include/ConcurrentMatchingEngine.h"
#include <chrono>
#include <thread>
#include <exception>

ConcurrentMatchingEngine::ConcurrentMatchingEngine(size_t buffer_size)
    : buffer_(buffer_size)
    , running_(false)
    , processed_count_(0)
    , dropped_count_(0)
    , policy_(BackpressurePolicy::REJECT)
{
}

ConcurrentMatchingEngine::~ConcurrentMatchingEngine() {
    stop();
}

void ConcurrentMatchingEngine::start() {
    std::lock_guard<std::mutex> lock(start_stop_mutex_);

    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return; // already running
    }
    consumer_thread_ = std::thread(&ConcurrentMatchingEngine::consumerLoop, this);
}

void ConcurrentMatchingEngine::stop() {
    std::lock_guard<std::mutex> lock(start_stop_mutex_);

    if (consumer_thread_.joinable() &&
        std::this_thread::get_id() == consumer_thread_.get_id()) {
        running_.store(false);
        return; // self-stop, cannot join
    }

    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return; // already stopped or never started
    }

    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }
}

bool ConcurrentMatchingEngine::submitOrder(const Order& order) {
    // Reject immediately if engine is not running — no consumer to drain
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }

    switch (policy_.load(std::memory_order_relaxed)) {
        case BackpressurePolicy::REJECT:
            return buffer_.push(order);

        case BackpressurePolicy::BLOCK:
            while (running_.load(std::memory_order_acquire)) {
                if (buffer_.push(order)) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            return false;

        case BackpressurePolicy::DROP:
            if (buffer_.push(order)) {
                return true;
            }
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            return false;

        default:
            return false;
    }
}

size_t ConcurrentMatchingEngine::getProcessedCount() const {
    return processed_count_.load(std::memory_order_acquire);
}

void ConcurrentMatchingEngine::consumerLoop() {
    while (running_.load(std::memory_order_acquire) || !buffer_.isEmpty()) {
        Order order;
        if (buffer_.pop(order)) {
            try {
                engine_.processOrder(order);
                processed_count_.fetch_add(1, std::memory_order_release);
            } catch (const std::exception&) {
                // TODO: proper error handling Phase 11
            } catch (...) {
                // Never let exception escape thread entry point — process survival
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}