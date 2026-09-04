// include/MatchingEngine.h
#ifndef MATCHINGENGINE_H
#define MATCHINGENGINE_H

#include "OrderBook.h"
#include "Order.h"
#include "Trade.h"
#include <vector>

// MatchingEngine is the main entry point for order processing.
// It holds an OrderBook and processes incoming orders.
class MatchingEngine {
public:
    MatchingEngine() = default;

    // Process an incoming order, match it against the book, 
    // and return resulting trades.
    // If order is not fully filled (and is a resting order), 
    // it gets added to the book.
    std::vector<Trade> processOrder(Order& order);

    // Cancel an existing order by ID
    bool cancelOrder(uint64_t order_id);

    // Get current best bid/ask (for market data)
    double getBestBid() const;
    double getBestAsk() const;

    // Get total orders count
    size_t getOrderCount() const;

    // Print the order book (debug)
    void printBook() const;

private:
    OrderBook book_;
};

#endif // MATCHINGENGINE_H