// include/Trade.h
#ifndef TRADE_H
#define TRADE_H

#include <cstdint>
#include <string>
#include <chrono>
#include <stdexcept>
#include <cmath>

struct Trade {
    uint64_t trade_id;
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    uint64_t buy_trader_id;   // for audit/compliance
    uint64_t sell_trader_id;  // for audit/compliance
    double price;
    uint32_t quantity;
    std::chrono::nanoseconds timestamp;

    Trade(uint64_t tid, uint64_t bid, uint64_t sid,
          uint64_t btrader, uint64_t strader,
          double p, uint32_t qty)
        : trade_id(tid)
        , buy_order_id(bid)
        , sell_order_id(sid)
        , buy_trader_id(btrader)
        , sell_trader_id(strader)
        , price(p)
        , quantity(qty)
        , timestamp(std::chrono::steady_clock::now().time_since_epoch()) {

        if (tid == 0) {
            throw std::invalid_argument("Trade ID must be non-zero");
        }
        if (buy_order_id == sell_order_id) {
            throw std::invalid_argument("Trade cannot match an order against itself");
        }
        if (quantity == 0) {
            throw std::invalid_argument("Trade quantity must be positive");
        }
        if (price <= 0.0 || std::isnan(price) || std::isinf(price)) {
            throw std::invalid_argument("Trade price must be positive and finite");
        }
    }

    std::string to_string() const {
        return "Trade[" + std::to_string(trade_id) + "] " +
               std::to_string(quantity) + " @ " + std::to_string(price) +
               " (Buy:" + std::to_string(buy_order_id) +
               ", Sell:" + std::to_string(sell_order_id) + ")";
    }
};

static_assert(sizeof(Trade) == 64, "Trade should fit one cache line");

#endif // TRADE_H