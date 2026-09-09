#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include "Order.h"
#include "Trade.h"
#include "OrderQueue.h"
#include "OrderPool.h"

enum class STPPolicy {
    NONE,
    CANCEL_NEWEST,
    CANCEL_OLDEST,
    CANCEL_BOTH
};

// Result returned by replaceOrder — success flag plus any trades generated.
struct ReplaceResult {
    bool success = false;
    std::vector<Trade> trades;
};

class OrderBook {
public:
    explicit OrderBook(size_t pool_capacity = 2000000);

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    void addOrder(const Order& order);
    bool cancelOrder(uint64_t order_id);
    ReplaceResult replaceOrder(uint64_t order_id, double new_price, uint32_t new_qty);
    std::vector<Trade> matchOrder(Order& incoming);

    // Recovery helpers
    bool getOrderById(uint64_t order_id, Order& out) const;
    bool applyFill(uint64_t order_id, uint32_t fill_qty);

    double getBestBid() const;
    double getBestAsk() const;
    size_t getOrderCount() const;
    void printBook() const;
    void setSTPPolicy(STPPolicy policy) { stp_policy_ = policy; }
    STPPolicy getSTPPolicy() const { return stp_policy_; }

private:
    struct PriceLevel {
        double price;
        OrderQueue orders;

        PriceLevel(OrderPool* pool, double p)
            : price(p), orders(pool) {}

        PriceLevel(const PriceLevel&) = delete;
        PriceLevel& operator=(const PriceLevel&) = delete;
        PriceLevel(PriceLevel&&) noexcept = default;
        PriceLevel& operator=(PriceLevel&&) noexcept = default;
    };

    std::vector<PriceLevel> bids_;
    std::vector<PriceLevel> asks_;

    uint64_t next_trade_id_;
    STPPolicy stp_policy_;
    OrderPool order_pool_;

    bool canFullyFill(const Order& incoming) const;
    bool wouldSelfTrade(const Order& incoming) const;
    size_t findBidLevel(double price) const;
    size_t findAskLevel(double price) const;
};

#endif // ORDERBOOK_H