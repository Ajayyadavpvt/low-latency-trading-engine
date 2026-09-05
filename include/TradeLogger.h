// include/TradeLogger.h
#ifndef TRADELOGGER_H
#define TRADELOGGER_H

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <string>
#include <atomic>
#include <cstddef>
#include "Trade.h"

class TradeLogger {
public:
    explicit TradeLogger(const std::string& filename, size_t max_queue_size = 10000);
    ~TradeLogger();

    bool log(const Trade& trade);
    void stop();
    bool hasError() const { return error_.load(std::memory_order_relaxed); }

private:
    void workerLoop();

    #pragma pack(push, 1)
    struct BinaryTrade {
        uint64_t trade_id;
        uint64_t buy_order_id;
        uint64_t sell_order_id;
        uint64_t buy_trader_id;
        uint64_t sell_trader_id;
        double price;
        uint32_t quantity;
        int64_t timestamp_ns;
    };
    #pragma pack(pop)

    std::ofstream file_;
    std::queue<Trade> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread worker_;
    bool stop_ = false;
    std::atomic<bool> error_{false};
    std::atomic<bool> stopped_{false};
    size_t max_queue_size_;
};

#endif // TRADELOGGER_H