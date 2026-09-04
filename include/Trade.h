// include/Trade.h
#ifndef TRADE_H
#define TRADE_H

#include <cstdint>
#include <string>
#include <chrono>

// Represents a matched trade between a buy and sell order
struct Trade {
    uint64_t trade_id;          // Unique trade identifier
    uint64_t buy_order_id;      // The buy order that participated
    uint64_t sell_order_id;     // The sell order that participated
    double price;               // Price at which trade executed
    uint32_t quantity;          // Quantity traded (shares)
    std::chrono::nanoseconds timestamp;  // Trade execution time

    // Constructor for easy creation
    Trade(uint64_t tid, uint64_t bid, uint64_t sid, double p, uint32_t qty)
        : trade_id(tid)
        , buy_order_id(bid)
        , sell_order_id(sid)
        , price(p)
        , quantity(qty)
        , timestamp(std::chrono::high_resolution_clock::now().time_since_epoch()) {}

    // Helper to print trade details
    std::string to_string() const {
        return "Trade[" + std::to_string(trade_id) + "] " +
               std::to_string(quantity) + " @ " + std::to_string(price) +
               " (Buy:" + std::to_string(buy_order_id) +
               ", Sell:" + std::to_string(sell_order_id) + ")";
    }
};

#endif // TRADE_H