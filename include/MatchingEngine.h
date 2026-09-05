// include/MatchingEngine.h
#ifndef MATCHINGENGINE_H
#define MATCHINGENGINE_H

#include "OrderBook.h"
#include "Order.h"
#include "Trade.h"
#include <vector>

class MatchingEngine {
public:
    MatchingEngine() = default;

    std::vector<Trade> processOrder(Order& order);
    bool cancelOrder(uint64_t order_id);

    double getBestBid() const;
    double getBestAsk() const;
    size_t getOrderCount() const;
    void printBook() const;

    // STP configuration — forwarded to OrderBook
    void setSTPPolicy(STPPolicy policy);
    STPPolicy getSTPPolicy() const;

private:
    OrderBook book_;
};

#endif