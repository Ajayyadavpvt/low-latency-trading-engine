// src/MatchingEngine.cpp
#include "MatchingEngine.h"
#include <chrono>

// Helper: current epoch timestamp in nanoseconds
static std::uint64_t getEpochTimestamp() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

// Helper: convert double price to integer ticks (1e6 ticks per unit)
static std::int64_t doubleToTicks(double price) {
    return static_cast<std::int64_t>(price * 1000000.0 + 0.5);
}

std::vector<Trade> MatchingEngine::processOrder(Order& order) {
    // ----- Event: OrderAccepted (temporary placement) -----
    // Ideally this should be after validation, but current OrderBook
    // does not expose rejection before matching. We'll improve later.
    if (publisher_) {
        publisher_->publish(OrderAcceptedEvent(
            getEpochTimestamp(),
            order.order_id,
            static_cast<std::uint32_t>(order.symbol_id),
            static_cast<std::uint32_t>(order.trader_id),
            (order.side == OrderSide::BUY),       // isBuy
            doubleToTicks(order.price),
            order.quantity
        ));
    }

    // Perform matching
    std::vector<Trade> trades = book_.matchOrder(order);

    // ----- Event: Trade for each executed trade -----
    for (const auto& trade : trades) {
        if (publisher_) {
            std::uint64_t resting_order_id, aggressor_order_id;
            std::uint32_t aggressor_trader_id;
            bool aggressor_is_buy;

            if (order.side == OrderSide::BUY) {
                // Incoming buy order is aggressor
                resting_order_id = trade.sell_order_id;
                aggressor_order_id = trade.buy_order_id;   // == order.order_id
                aggressor_trader_id = static_cast<std::uint32_t>(trade.buy_trader_id);
                aggressor_is_buy = true;
            } else { // SELL
                resting_order_id = trade.buy_order_id;
                aggressor_order_id = trade.sell_order_id;  // == order.order_id
                aggressor_trader_id = static_cast<std::uint32_t>(trade.sell_trader_id);
                aggressor_is_buy = false;
            }

            publisher_->publish(TradeEvent(
                getEpochTimestamp(),          // per-trade timestamp
                resting_order_id,
                aggressor_order_id,
                static_cast<std::uint32_t>(order.symbol_id),
                aggressor_trader_id,
                trade.quantity,
                doubleToTicks(trade.price),
                aggressor_is_buy
            ));
        }
    }

    return trades;
}

bool MatchingEngine::cancelOrder(uint64_t order_id) {
    // TODO: Emit OrderCancelledEvent after actual cancellation
    //       (requires OrderBook integration)
    return book_.cancelOrder(order_id);
}

ReplaceResult MatchingEngine::replaceOrder(
    uint64_t order_id,
    double new_price,
    uint32_t new_qty)
{
    // TODO: Emit appropriate events (cancel + accept, or update)
    //       (requires OrderBook integration)
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