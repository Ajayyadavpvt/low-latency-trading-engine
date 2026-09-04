// src/MatchingEngine.cpp
#include "../include/MatchingEngine.h"

// Process an incoming order:
// 1. Match it against existing orders in the book.
// 2. If it cannot be fully matched and is a resting order (LIMIT), add to book.
// 3. Return vector of trades executed.
std::vector<Trade> MatchingEngine::processOrder(Order& order) {
    return book_.matchOrder(order);
}

// Cancel an order by ID, return success/failure
bool MatchingEngine::cancelOrder(uint64_t order_id) {
    return book_.cancelOrder(order_id);
}

// Get current best bid price
double MatchingEngine::getBestBid() const {
    return book_.getBestBid();
}

// Get current best ask price
double MatchingEngine::getBestAsk() const {
    return book_.getBestAsk();
}

// Get total number of orders in the book
size_t MatchingEngine::getOrderCount() const {
    return book_.getOrderCount();
}

// Print the current order book
void MatchingEngine::printBook() const {
    book_.printBook();
}