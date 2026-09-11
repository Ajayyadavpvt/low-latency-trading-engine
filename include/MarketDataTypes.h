#pragma once
#include <cstdint>
#include <variant>
#include "Order.h"

enum class EventType : uint8_t {
    OrderAccepted,
    OrderRejected,
    OrderCancelled,
    Trade,
    OrderFilled,
    OrderPartiallyFilled
};

struct OrderAcceptedEvent {
    std::uint64_t timestamp;
    std::uint64_t sequence;
    std::uint64_t orderId;
    std::uint32_t symbolId;
    std::uint32_t traderId;
    bool isBuy;
    std::int64_t priceTicks;
    std::uint32_t quantity;
    std::uint32_t remainingQuantity;
    OrderType orderType;
    std::uint64_t prioritySeq;   // <-- NEW: Feature E

    OrderAcceptedEvent(
        std::uint64_t ts, std::uint64_t seq, std::uint64_t oid,
        std::uint32_t sid, std::uint32_t tid,
        bool buy, std::int64_t px, std::uint32_t qty,
        std::uint32_t remQty, OrderType type,
        std::uint64_t priority)   // <-- NEW parameter
        : timestamp(ts), sequence(seq), orderId(oid),
          symbolId(sid), traderId(tid),
          isBuy(buy), priceTicks(px), quantity(qty),
          remainingQuantity(remQty), orderType(type),
          prioritySeq(priority) {}
};

struct TradeEvent {
    std::uint64_t timestamp;
    std::uint64_t sequence;
    std::uint64_t restingOrderId;
    std::uint64_t aggressorOrderId;
    std::uint32_t symbolId;
    std::uint32_t traderId;
    std::uint32_t tradeQuantity;
    std::int64_t tradePriceTicks;
    bool aggressorIsBuy;

    TradeEvent(
        std::uint64_t ts, std::uint64_t seq, std::uint64_t rest,
        std::uint64_t aggr, std::uint32_t sid, std::uint32_t tid,
        std::uint32_t qty, std::int64_t px, bool buy)
        : timestamp(ts), sequence(seq), restingOrderId(rest),
          aggressorOrderId(aggr), symbolId(sid), traderId(tid),
          tradeQuantity(qty), tradePriceTicks(px), aggressorIsBuy(buy) {}
};

struct OrderCancelledEvent {
    std::uint64_t timestamp;
    std::uint64_t sequence;
    std::uint64_t orderId;
    std::uint32_t symbolId;
    std::uint32_t traderId;
    std::uint32_t cancelledQuantity;

    OrderCancelledEvent(
        std::uint64_t ts, std::uint64_t seq, std::uint64_t oid,
        std::uint32_t sid, std::uint32_t tid, std::uint32_t qty)
        : timestamp(ts), sequence(seq), orderId(oid),
          symbolId(sid), traderId(tid), cancelledQuantity(qty) {}
};

struct OrderRejectedEvent {
    std::uint64_t timestamp;
    std::uint64_t sequence;
    std::uint64_t orderId;
    std::uint32_t symbolId;
    std::uint32_t traderId;

    OrderRejectedEvent(
        std::uint64_t ts, std::uint64_t seq, std::uint64_t oid,
        std::uint32_t sid, std::uint32_t tid)
        : timestamp(ts), sequence(seq), orderId(oid),
          symbolId(sid), traderId(tid) {}
};

using MarketEvent = std::variant<OrderAcceptedEvent,
                                 TradeEvent,
                                 OrderCancelledEvent,
                                 OrderRejectedEvent>;