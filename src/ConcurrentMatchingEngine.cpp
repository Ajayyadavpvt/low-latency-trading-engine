// src/ConcurrentMatchingEngine.cpp
#include "../include/ConcurrentMatchingEngine.h"
#include <chrono>

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
    if (running_.load()) return;
    running_.store(true);
    consumer_thread_ = std::thread(&ConcurrentMatchingEngine::consumerLoop, this);
}

void ConcurrentMatchingEngine::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }
}

bool ConcurrentMatchingEngine::submitOrder(const Order& order) {
    return buffer_.push(order);
}

size_t ConcurrentMatchingEngine::getProcessedCount() const {
    return processed_count_.load();
}

void ConcurrentMatchingEngine::consumerLoop() {
    while (running_.load()) {
        Order order;
        if (buffer_.pop(order)) {
            engine_.processOrder(order);
            processed_count_.fetch_add(1);
        } else {
            // Small sleep to avoid busy spin when empty
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}