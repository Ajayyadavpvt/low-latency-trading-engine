// include/OrderBook.h
#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include "Order.h"
#include "Trade.h"

enum class STPPolicy {
    NONE,
    CANCEL_NEWEST,
    CANCEL_OLDEST,
    CANCEL_BOTH
};

class OrderBook {
public:
    OrderBook();
    void addOrder(const Order& order);
    bool cancelOrder(uint64_t order_id);
    std::vector<Trade> matchOrder(Order& incoming);
    double getBestBid() const;
    double getBestAsk() const;
    size_t getOrderCount() const;
    void printBook() const;
    void setSTPPolicy(STPPolicy policy) { stp_policy_ = policy; }
    STPPolicy getSTPPolicy() const { return stp_policy_; }

private:
    struct PriceLevel {
        double price;
        std::vector<Order> orders;
    };

    std::vector<PriceLevel> bids_;  // sorted descending
    std::vector<PriceLevel> asks_;  // sorted ascending

    uint64_t next_trade_id_;
    STPPolicy stp_policy_;

    bool canFullyFill(const Order& incoming) const;
    bool wouldSelfTrade(const Order& incoming) const;
    size_t findBidLevel(double price) const;
    size_t findAskLevel(double price) const;
};

// NOTE: This class is NOT thread-safe. It must only be accessed from
// a single thread (typically the consumer thread in ConcurrentMatchingEngine).

#endif // ORDERBOOK_H