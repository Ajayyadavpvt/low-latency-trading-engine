// src/MatchingEngine.cpp
#include "../include/MatchingEngine.h"

std::vector<Trade> MatchingEngine::processOrder(Order& order) {
    return book_.matchOrder(order);
}

bool MatchingEngine::cancelOrder(uint64_t order_id) {
    return book_.cancelOrder(order_id);
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