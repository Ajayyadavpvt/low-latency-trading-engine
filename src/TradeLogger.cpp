// src/TradeLogger.cpp
#include "../include/TradeLogger.h"
#include <stdexcept>
#include <iostream>

TradeLogger::TradeLogger(const std::string& filename, size_t max_queue_size)
    : file_(filename, std::ios::binary | std::ios::app)
    , max_queue_size_(max_queue_size)
{
    if (max_queue_size_ == 0) {
        throw std::invalid_argument("TradeLogger: max_queue_size must be > 0");
    }
    if (!file_.is_open()) {
        throw std::runtime_error("TradeLogger: cannot open file " + filename);
    }
    worker_ = std::thread(&TradeLogger::workerLoop, this);
}

TradeLogger::~TradeLogger() {
    stop();
}

bool TradeLogger::log(const Trade& trade) {
    if (error_.load(std::memory_order_relaxed)) return false;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stop_) return false;
        if (queue_.size() >= max_queue_size_) return false;

        bool was_empty = queue_.empty();
        try {
            queue_.push(trade);
        } catch (...) {
            return false;
        }

        if (was_empty) {
            cv_.notify_one();
        }
    }
    return true;
}

void TradeLogger::stop() {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) {
        if (worker_.joinable()) worker_.join();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mtx_);
        stop_ = true;
    }
    cv_.notify_one();

    if (worker_.joinable()) worker_.join();

    try {
        if (file_.is_open()) file_.flush();
    } catch (...) {
        error_.store(true, std::memory_order_relaxed);
    }
}

void TradeLogger::workerLoop() {
    try {
        while (true) {
            std::unique_lock<std::mutex> lock(mtx_);

            cv_.wait(lock, [this] {
                return !queue_.empty() || stop_;
            });

            while (!queue_.empty() && !error_.load(std::memory_order_relaxed)) {
                Trade trade = std::move(queue_.front());
                queue_.pop();
                lock.unlock();

                BinaryTrade bin;
                bin.trade_id = trade.trade_id;
                bin.buy_order_id = trade.buy_order_id;
                bin.sell_order_id = trade.sell_order_id;
                bin.buy_trader_id = trade.buy_trader_id;
                bin.sell_trader_id = trade.sell_trader_id;
                bin.price = trade.price;
                bin.quantity = trade.quantity;
                // FIX: timestamp is already nanoseconds — direct count()
                bin.timestamp_ns = static_cast<int64_t>(trade.timestamp.count());

                file_.write(reinterpret_cast<const char*>(&bin), sizeof(bin));

                if (!file_.good()) {
                    error_.store(true, std::memory_order_relaxed);
                    lock.lock();
                    break;
                }

                lock.lock();
            }

            if (error_.load(std::memory_order_relaxed)) {
                if (queue_.empty()) break;
            }
            if (stop_ && queue_.empty()) {
                break;
            }
        }
    } catch (const std::exception& e) {
        error_.store(true, std::memory_order_relaxed);
        std::cerr << "TradeLogger error: " << e.what() << "\n";
    } catch (...) {
        error_.store(true, std::memory_order_relaxed);
        std::cerr << "TradeLogger: unknown error\n";
    }
}