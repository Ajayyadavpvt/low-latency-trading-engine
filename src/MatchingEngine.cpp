// src/MatchingEngine.cpp
#include "MatchingEngine.h"

std::vector<Trade> MatchingEngine::processOrder(Order& order) {
    return book_.matchOrder(order);
}

bool MatchingEngine::cancelOrder(uint64_t order_id) {
    return book_.cancelOrder(order_id);
}

ReplaceResult MatchingEngine::replaceOrder(
    uint64_t order_id,
    double new_price,
    uint32_t new_qty)
{
    return book_.replaceOrder(order_id, new_price, new_qty);
}

double MatchingEngine::getBestBid() const {
    return book_.getBestBid();
}

double MatchingEngine::getBestAsk() const {
    return book_.getBestAsk();
}

size_t MatchingEngine::getOrderCount() const {
    return book_.getOrderCount();
}

void MatchingEngine::printBook() const {
    book_.printBook();
}

void MatchingEngine::setSTPPolicy(STPPolicy policy) {
    book_.setSTPPolicy(policy);
}

STPPolicy MatchingEngine::getSTPPolicy() const {
    return book_.getSTPPolicy();
}