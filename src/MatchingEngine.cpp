#include "MatchingEngine.h"
#include "Journal.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

static std::uint64_t getEpochTimestamp() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(
        system_clock::now().time_since_epoch()).count();
}

static std::int64_t doubleToTicks(double price) {
    return static_cast<std::int64_t>(
        price * 1000000.0 + 0.5);
}

std::uint64_t MatchingEngine::nextSequence() {
    if (sequence_counter_) {
        return sequence_counter_->fetch_add(
            1,
            std::memory_order_relaxed);
    }
    return 0;
}

bool MatchingEngine::isHealthy() const {
    if (state() == EngineState::HALTED) {
        return false;
    }

    if (journal_ && !journal_->isHealthy()) {
        return false;
    }

    return true;
}

std::vector<Trade> MatchingEngine::processOrder(Order& order) {
    if (!isHealthy()) {
        return {};
    }

    std::uint64_t priority_seq = 0;

    if (!tryNextPriority(priority_seq)) {
        halt();
        return {};
    }

    order.priority_seq = priority_seq;

    std::vector<Trade> trades = book_.matchOrder(order);

    const std::uint64_t ts = getEpochTimestamp();

    if (publisher_) {
        for (const auto& trade : trades) {
            std::uint64_t resting_id;
            std::uint64_t aggressor_id;
            std::uint32_t aggressor_trader;
            bool aggressor_is_buy;

            if (order.side == OrderSide::BUY) {
                resting_id = trade.sell_order_id;
                aggressor_id = trade.buy_order_id;
                aggressor_trader =
                    static_cast<std::uint32_t>(trade.buy_trader_id);
                aggressor_is_buy = true;
            } else {
                resting_id = trade.buy_order_id;
                aggressor_id = trade.sell_order_id;
                aggressor_trader =
                    static_cast<std::uint32_t>(trade.sell_trader_id);
                aggressor_is_buy = false;
            }

            publisher_->publish(
                TradeEvent(
                    ts,
                    nextSequence(),
                    resting_id,
                    aggressor_id,
                    static_cast<std::uint32_t>(order.symbol_id),
                    aggressor_trader,
                    trade.quantity,
                    doubleToTicks(trade.price),
                    aggressor_is_buy));
        }

        // MARKET orders can never rest.
        if (order.remaining_quantity > 0 &&
            order.type != OrderType::MARKET &&
            order.type != OrderType::IOC &&
            order.type != OrderType::FOK) {

            publisher_->publish(
                OrderAcceptedEvent(
                    ts,
                    nextSequence(),
                    order.order_id,
                    static_cast<std::uint32_t>(order.symbol_id),
                    static_cast<std::uint32_t>(order.trader_id),
                    order.side == OrderSide::BUY,
                    doubleToTicks(order.price),
                    order.quantity,
                    order.remaining_quantity,
                    order.type,
                    order.priority_seq));
        }
    }

    return trades;
}

bool MatchingEngine::cancelOrder(uint64_t order_id) {
    if (!isHealthy()) {
        return false;
    }

    Order old_order;

    if (!book_.getOrderById(order_id, old_order)) {
        return false;
    }

    if (!book_.cancelOrder(order_id)) {
        return false;
    }

    if (publisher_) {
        publisher_->publish(
            OrderCancelledEvent(
                getEpochTimestamp(),
                nextSequence(),
                order_id,
                static_cast<std::uint32_t>(old_order.symbol_id),
                static_cast<std::uint32_t>(old_order.trader_id),
                old_order.remaining_quantity));
    }

    return true;
}

ReplaceResult MatchingEngine::replaceOrder(
    uint64_t order_id,
    double new_price,
    uint32_t new_qty)
{
    if (!isHealthy()) {
        return ReplaceResult{false, {}, 0};
    }

    Order old_order;

    if (!book_.getOrderById(order_id, old_order)) {
        return ReplaceResult{false, {}, 0};
    }

    if (new_qty > 0 &&
        (!std::isfinite(new_price) || new_price <= 0.0)) {
        return ReplaceResult{false, {}, 0};
    }

    const bool same_price = (new_price == old_order.price);
    const bool quantity_increased = (new_qty > old_order.quantity);
    const bool loses_priority = !same_price || quantity_increased;

    std::uint64_t replacement_priority = old_order.priority_seq;

    if (new_qty > 0 && loses_priority) {
        if (!tryNextPriority(replacement_priority)) {
            halt();
            return ReplaceResult{false, {}, 0};
        }
    }

    ReplaceResult result = book_.replaceOrder(
        order_id,
        new_price,
        new_qty,
        replacement_priority);

    if (!result.success) {
        return result;
    }

    if (publisher_) {
        const std::uint64_t ts = getEpochTimestamp();

        publisher_->publish(
            OrderCancelledEvent(
                ts,
                nextSequence(),
                order_id,
                static_cast<std::uint32_t>(old_order.symbol_id),
                static_cast<std::uint32_t>(old_order.trader_id),
                old_order.remaining_quantity));

        for (const auto& trade : result.trades) {
            std::uint64_t resting_id;
            std::uint64_t aggressor_id;
            std::uint32_t aggressor_trader;
            bool aggressor_is_buy;

            if (old_order.side == OrderSide::BUY) {
                resting_id = trade.sell_order_id;
                aggressor_id = trade.buy_order_id;
                aggressor_trader =
                    static_cast<std::uint32_t>(trade.buy_trader_id);
                aggressor_is_buy = true;
            } else {
                resting_id = trade.buy_order_id;
                aggressor_id = trade.sell_order_id;
                aggressor_trader =
                    static_cast<std::uint32_t>(trade.sell_trader_id);
                aggressor_is_buy = false;
            }

            publisher_->publish(
                TradeEvent(
                    ts,
                    nextSequence(),
                    resting_id,
                    aggressor_id,
                    static_cast<std::uint32_t>(old_order.symbol_id),
                    aggressor_trader,
                    trade.quantity,
                    doubleToTicks(trade.price),
                    aggressor_is_buy));
        }

        if (new_qty > 0 &&
            result.final_remaining_quantity > 0 &&
            old_order.type != OrderType::MARKET &&
            old_order.type != OrderType::IOC &&
            old_order.type != OrderType::FOK) {

            publisher_->publish(
                OrderAcceptedEvent(
                    ts,
                    nextSequence(),
                    order_id,
                    static_cast<std::uint32_t>(old_order.symbol_id),
                    static_cast<std::uint32_t>(old_order.trader_id),
                    old_order.side == OrderSide::BUY,
                    doubleToTicks(new_price),
                    new_qty,
                    result.final_remaining_quantity,
                    old_order.type,
                    replacement_priority));
        }
    }

    return result;
}

bool MatchingEngine::restoreOrder(
    const Order& order,
    uint32_t remaining_quantity)
{
    if (order.type != OrderType::LIMIT) {
        return false;
    }

    if (remaining_quantity == 0 ||
        remaining_quantity > order.quantity) {
        return false;
    }

    if (!std::isfinite(order.price) || order.price <= 0.0) {
        return false;
    }

    Order existing;

    if (book_.getOrderById(order.order_id, existing)) {
        return false;
    }

    Order restored = order;
    restored.remaining_quantity = remaining_quantity;

    return book_.addOrder(restored);
}

bool MatchingEngine::restoreCancel(uint64_t order_id) {
    return book_.cancelOrder(order_id);
}

bool MatchingEngine::applyFill(
    uint64_t order_id,
    uint32_t fill_qty)
{
    return book_.applyFill(order_id, fill_qty);
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

bool MatchingEngine::getOrderById(
    uint64_t order_id,
    Order& out) const
{
    return book_.getOrderById(order_id, out);
}