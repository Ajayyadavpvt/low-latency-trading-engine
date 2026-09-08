#ifndef RISKENGINE_H
#define RISKENGINE_H

#include "Order.h"
#include "SymbolId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

enum class RiskRejectReason : uint8_t {
    NONE = 0,
    INVALID_SYMBOL,
    INVALID_QUANTITY,
    ORDER_TOO_LARGE,
    POSITION_LIMIT_EXCEEDED,
    PRICE_BAND_VIOLATION,
    INVALID_PRICE,
    STATE_VIOLATION,
    INVALID_CONFIGURATION,
    UNKNOWN
};

// Pre-trade risk engine.
//
// THREAD SAFETY:
// - One RiskEngine instance belongs to exactly one shard.
// - Only that shard's consumer thread may call:
//     validate()
//     onOrderAccepted()
//     onFill()
//     onOrderCancelled()
//     setReferencePrice()
//     configuration setters
// - No internal locking is required.
// - getPosition() is also intended for the owner thread only.
//
// LIFECYCLE:
//
//     validate(order)
//          |
//          v
//     onOrderAccepted(order)
//          |
//          +----> onFill(order, qty)
//          |
//          +----> onOrderCancelled(order, qty)
//
// onFill() moves quantity from working exposure to executed position.
// onOrderCancelled() releases remaining working exposure.
class RiskEngine {
public:
    RiskEngine() = default;

    // Configuration should be completed before trading starts.
    void setMaxOrderSize(uint64_t max_qty) noexcept;
    void setMaxPosition(uint64_t max_pos) noexcept;
    void setPriceBandPct(double pct) noexcept;

    // Registers/updates a reference price.
    // Returns false if symbol is outside the supported symbol range
    // or the price is invalid.
    bool setReferencePrice(SymbolId symbol, double price);

    // Reserve enough hash-table capacity before trading starts.
    void reservePositions(std::size_t expected_pairs);

    // Read-only pre-trade validation.
    // Does NOT modify risk state.
    RiskRejectReason validate(const Order& order) const noexcept;

    // State transitions. Owner thread only.
    bool onOrderAccepted(const Order& order) noexcept;
    bool onFill(const Order& order, uint64_t fill_qty) noexcept;
    bool onOrderCancelled(const Order& order,
                          uint64_t cancelled_qty) noexcept;

    int64_t getPosition(uint64_t trader_id,
                        SymbolId symbol) const noexcept;

private:
    struct PositionKey {
        uint64_t trader_id;
        SymbolId symbol_id;

        bool operator==(const PositionKey& other) const noexcept {
            return trader_id == other.trader_id &&
                   symbol_id == other.symbol_id;
        }
    };

    struct PositionKeyHash {
        std::size_t operator()(const PositionKey& key) const noexcept {
            // Hash the two components independently instead of relying
            // on truncation/bit packing assumptions about SymbolId.
            const std::size_t h1 =
                std::hash<uint64_t>{}(key.trader_id);

            const std::size_t h2 =
                std::hash<uint32_t>{}(
                    static_cast<uint32_t>(key.symbol_id));

            return h1 ^
                   (h2 + static_cast<std::size_t>(0x9e3779b9) +
                    (h1 << 6) + (h1 >> 2));
        }
    };

    struct PositionState {
        // Executed net position.
        int64_t position = 0;

        // Accepted but not yet executed quantity.
        uint64_t working_buy = 0;
        uint64_t working_sell = 0;
    };

    static constexpr std::size_t MAX_SYMBOLS = 1'000'000;

    uint64_t max_order_qty_ = 1'000'000;
    uint64_t max_position_ = 10'000'000;

    // Kept as double for compatibility with the current Order API.
    // Production version should migrate to integer ticks / fixed-point.
    double price_band_pct_ = 5.0;

    std::vector<double> ref_prices_;
    std::vector<uint8_t> ref_price_set_;

    std::unordered_map<
        PositionKey,
        PositionState,
        PositionKeyHash
    > positions_;

    static bool canAddSigned(int64_t a,
                             int64_t b,
                             int64_t& result) noexcept;

    static bool canSubSigned(int64_t a,
                             int64_t b,
                             int64_t& result) noexcept;

    static bool canAddUnsigned(uint64_t a,
                               uint64_t b,
                               uint64_t& result) noexcept;
};

#endif // RISKENGINE_H