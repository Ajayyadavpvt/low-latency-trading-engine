// include/MatchingEngine.h
#ifndef MATCHINGENGINE_H
#define MATCHINGENGINE_H

#include "OrderBook.h"
#include "Order.h"
#include "Trade.h"
#include "MarketDataPublisher.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// Single-threaded wrapper around OrderBook.
// NOT thread-safe — must be owned and called exclusively by one shard's
// consumer thread.
class MatchingEngine {
public:
    MatchingEngine() = default;

    // Incoming order may be modified (remaining_quantity) during matching.
    std::vector<Trade> processOrder(Order& order);

    bool cancelOrder(uint64_t order_id);

    ReplaceResult replaceOrder(uint64_t order_id, double new_price, uint32_t new_qty);

    double getBestBid() const;
    double getBestAsk() const;
    size_t getOrderCount() const;
    void printBook() const;

    void setSTPPolicy(STPPolicy policy);
    STPPolicy getSTPPolicy() const;

    // Market data publisher setter (optional)
    void setMarketDataPublisher(MarketDataPublisher* publisher) {
        publisher_ = publisher;
    }

private:
    OrderBook book_;
    MarketDataPublisher* publisher_ = nullptr;
};

#endif // MATCHINGENGINE_H