#pragma once
#include "OrderBook.h"
#include "Order.h"
#include "Trade.h"
#include "MarketDataPublisher.h"
#include <atomic>
#include <cstdint>
#include <vector>

// Forward declaration to avoid circular include
class Journal;

class MatchingEngine {
public:
    MatchingEngine() = default;

    std::vector<Trade> processOrder(Order& order);
    bool cancelOrder(uint64_t order_id);
    ReplaceResult replaceOrder(uint64_t order_id, double new_price, uint32_t new_qty);

    // Recovery: add order without matching
    bool restoreOrder(const Order& order, uint32_t remaining_quantity);

    // Recovery: cancel order without publishing event
    bool restoreCancel(uint64_t order_id);

    // Recovery: apply a fill to a resting order
    bool applyFill(uint64_t order_id, uint32_t fill_qty);

    // Lookup order in book (used by tests/recovery)
    bool getOrderById(uint64_t order_id, Order& out) const;

    double getBestBid() const;
    double getBestAsk() const;
    size_t getOrderCount() const;
    void printBook() const;

    void setSTPPolicy(STPPolicy policy);
    STPPolicy getSTPPolicy() const;

    void setMarketDataPublisher(MarketDataPublisher* publisher) {
        publisher_ = publisher;
    }

    void setSequenceCounter(std::atomic<std::uint64_t>* counter) {
        sequence_counter_ = counter;
    }

    void seedSequence(std::uint64_t next_seq) {
        if (sequence_counter_) {
            sequence_counter_->store(next_seq, std::memory_order_release);
        }
    }

    // Attach a Journal for health monitoring
    void setJournal(Journal* journal) {
        journal_ = journal;
    }

    // Returns true if engine is safe to accept new orders.
    // False if attached Journal is unhealthy (write failure / queue overflow).
    bool isHealthy() const;

private:
    std::uint64_t nextSequence();

    OrderBook book_;
    MarketDataPublisher* publisher_ = nullptr;
    std::atomic<std::uint64_t>* sequence_counter_ = nullptr;
    Journal* journal_ = nullptr;
};