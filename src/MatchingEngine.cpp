#include "MatchingEngine.h"
#include <chrono>

static std::uint64_t getEpochTimestamp() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

static std::int64_t doubleToTicks(double price) {
    return static_cast<std::int64_t>(price * 1000000.0 + 0.5);
}

std::uint64_t MatchingEngine::nextSequence() {
    if (sequence_counter_) {
        return sequence_counter_->fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
}

std::vector<Trade> MatchingEngine::processOrder(Order& order) {
    std::vector<Trade> trades = book_.matchOrder(order);
    const std::uint64_t ts = getEpochTimestamp();

    if (publisher_) {
        // Trade events
        for (const auto& trade : trades) {
            std::uint64_t resting_id, aggressor_id;
            std::uint32_t aggressor_trader;
            bool aggressor_is_buy;

            if (order.side == OrderSide::BUY) {
                resting_id = trade.sell_order_id;
                aggressor_id = trade.buy_order_id;
                aggressor_trader = static_cast<std::uint32_t>(trade.buy_trader_id);
                aggressor_is_buy = true;
            } else {
                resting_id = trade.buy_order_id;
                aggressor_id = trade.sell_order_id;
                aggressor_trader = static_cast<std::uint32_t>(trade.sell_trader_id);
                aggressor_is_buy = false;
            }

            publisher_->publish(TradeEvent(
                ts, nextSequence(), resting_id, aggressor_id,
                static_cast<std::uint32_t>(order.symbol_id),
                aggressor_trader, trade.quantity,
                doubleToTicks(trade.price), aggressor_is_buy));
        }

        // Order accepted only if rests
        if (order.remaining_quantity > 0 &&
            order.type != OrderType::IOC &&
            order.type != OrderType::FOK) {
            publisher_->publish(OrderAcceptedEvent(
                ts, nextSequence(), order.order_id,
                static_cast<std::uint32_t>(order.symbol_id),
                static_cast<std::uint32_t>(order.trader_id),
                (order.side == OrderSide::BUY),
                doubleToTicks(order.price),
                order.quantity,
                order.remaining_quantity,
                order.type));
        }
    }
    return trades;
}

bool MatchingEngine::cancelOrder(uint64_t order_id) {
    Order old_order;
    bool found = book_.getOrderById(order_id, old_order);
    bool cancelled = book_.cancelOrder(order_id);

    if (cancelled && publisher_) {
        publisher_->publish(OrderCancelledEvent(
            getEpochTimestamp(), nextSequence(), order_id,
            static_cast<std::uint32_t>(old_order.symbol_id),
            static_cast<std::uint32_t>(old_order.trader_id),
            old_order.remaining_quantity));
    }
    return cancelled;
}

ReplaceResult MatchingEngine::replaceOrder(
    uint64_t order_id, double new_price, uint32_t new_qty) {

    Order old_order;
    bool old_found = book_.getOrderById(order_id, old_order);
    ReplaceResult result = book_.replaceOrder(order_id, new_price, new_qty);

    if (result.success && publisher_) {
        std::uint64_t ts = getEpochTimestamp();

        publisher_->publish(OrderCancelledEvent(
            ts, nextSequence(), order_id,
            static_cast<std::uint32_t>(old_order.symbol_id),
            static_cast<std::uint32_t>(old_order.trader_id),
            old_order.remaining_quantity));

        if (new_qty > 0) {
            publisher_->publish(OrderAcceptedEvent(
                ts, nextSequence(), order_id,
                static_cast<std::uint32_t>(old_order.symbol_id),
                static_cast<std::uint32_t>(old_order.trader_id),
                (old_order.side == OrderSide::BUY),
                doubleToTicks(new_price),
                new_qty,
                new_qty,
                old_order.type));
        }
    }
    return result;
}

bool MatchingEngine::restoreOrder(const Order& order, uint32_t remaining_quantity) {
    if (order.type != OrderType::LIMIT) return false;
    Order restored = order;
    restored.remaining_quantity = remaining_quantity;
    book_.addOrder(restored);
    return true;
}

bool MatchingEngine::applyFill(uint64_t order_id, uint32_t fill_qty) {
    return book_.applyFill(order_id, fill_qty);
}

double MatchingEngine::getBestBid() const { return book_.getBestBid(); }
double MatchingEngine::getBestAsk() const { return book_.getBestAsk(); }
size_t MatchingEngine::getOrderCount() const { return book_.getOrderCount(); }
void MatchingEngine::printBook() const { book_.printBook(); }
void MatchingEngine::setSTPPolicy(STPPolicy policy) { book_.setSTPPolicy(policy); }
STPPolicy MatchingEngine::getSTPPolicy() const { return book_.getSTPPolicy(); }