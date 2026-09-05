// include/OrderBook.h
#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <map>
#include <vector>
#include "Order.h"
#include "Trade.h"

// Self-trade prevention policy
enum class STPPolicy {
    NONE,           // no prevention (default)
    CANCEL_NEWEST,  // cancel incoming order
    CANCEL_OLDEST,  // cancel resting order
    CANCEL_BOTH     // cancel both orders
};

class OrderBook {
public:
    OrderBook();  // initializes next_trade_id_ and stp_policy_

    void addOrder(const Order& order);
    bool cancelOrder(uint64_t order_id);
    std::vector<Trade> matchOrder(Order& incoming);
    double getBestBid() const;
    double getBestAsk() const;
    size_t getOrderCount() const;
    void printBook() const;

    // STP configuration
    void setSTPPolicy(STPPolicy policy) { stp_policy_ = policy; }
    STPPolicy getSTPPolicy() const { return stp_policy_; }

private:
    std::map<double, std::vector<Order>, std::greater<double>> bids_;
    std::map<double, std::vector<Order>, std::less<double>> asks_;
    uint64_t next_trade_id_;
    STPPolicy stp_policy_;

    bool canFullyFill(const Order& incoming) const;
    bool wouldSelfTrade(const Order& incoming) const;
};

#endif // ORDERBOOK_H