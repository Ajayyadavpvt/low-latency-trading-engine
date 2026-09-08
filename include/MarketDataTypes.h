#pragma once
#include <cstdint>
#include <variant>

enum class EventType : uint8_t {
    OrderAccepted,
    OrderRejected,
    OrderCancelled,
    Trade,
    OrderFilled,
    OrderPartiallyFilled
};

// Price in integer ticks (e.g., multiply by 1e6)
// Timestamp in nanoseconds since Unix epoch (system_clock)
struct OrderAcceptedEvent {
    std::uint64_t timestamp;
    std::uint64_t orderId;
    std::uint32_t symbolId;
    std::uint32_t traderId;
    bool isBuy;
    std::int64_t priceTicks;      // 0 for market orders
    std::uint32_t quantity;

    OrderAcceptedEvent(std::uint64_t ts, std::uint64_t oid, std::uint32_t sid, std::uint32_t tid,
                       bool buy, std::int64_t px, std::uint32_t qty)
        : timestamp(ts), orderId(oid), symbolId(sid), traderId(tid),
          isBuy(buy), priceTicks(px), quantity(qty) {}
};

struct TradeEvent {
    std::uint64_t timestamp;
    std::uint64_t restingOrderId;   // maker
    std::uint64_t aggressorOrderId; // taker
    std::uint32_t symbolId;
    std::uint32_t traderId;         // aggressor's trader
    std::uint32_t tradeQuantity;
    std::int64_t tradePriceTicks;
    bool aggressorIsBuy;

    TradeEvent(std::uint64_t ts, std::uint64_t rest, std::uint64_t aggr, std::uint32_t sid,
               std::uint32_t tid, std::uint32_t qty, std::int64_t px, bool buy)
        : timestamp(ts), restingOrderId(rest), aggressorOrderId(aggr),
          symbolId(sid), traderId(tid), tradeQuantity(qty),
          tradePriceTicks(px), aggressorIsBuy(buy) {}
};

struct OrderCancelledEvent {
    std::uint64_t timestamp;
    std::uint64_t orderId;
    std::uint32_t symbolId;
    std::uint32_t traderId;
    std::uint32_t cancelledQuantity;

    OrderCancelledEvent(std::uint64_t ts, std::uint64_t oid, std::uint32_t sid, std::uint32_t tid,
                        std::uint32_t qty)
        : timestamp(ts), orderId(oid), symbolId(sid), traderId(tid),
          cancelledQuantity(qty) {}
};

struct OrderRejectedEvent {
    std::uint64_t timestamp;
    std::uint64_t orderId;
    std::uint32_t symbolId;
    std::uint32_t traderId;

    OrderRejectedEvent(std::uint64_t ts, std::uint64_t oid, std::uint32_t sid, std::uint32_t tid)
        : timestamp(ts), orderId(oid), symbolId(sid), traderId(tid) {}
};

// Main event variant
using MarketEvent = std::variant<OrderAcceptedEvent,
                                 TradeEvent,
                                 OrderCancelledEvent,
                                 OrderRejectedEvent>;