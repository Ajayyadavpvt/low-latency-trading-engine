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

// Forward declaration only. checkInvariant() takes a const reference.
class OrderBook;

// ---------------------------------------------------------------------------
// RiskRejectReason
// ---------------------------------------------------------------------------
// Existing values MUST NOT be reordered (they may be logged/serialized).
// New values are appended at the end.
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
    UNKNOWN,

    // ---- Appended for matching-engine integration ----
    NOTIONAL,
    STATE_CORRUPT,
    SHARD_HALTED
};

// ---------------------------------------------------------------------------
// RiskEngine — per-shard pre-trade / in-flight risk gate.
// ---------------------------------------------------------------------------
//
// THREAD SAFETY:
//   - One RiskEngine instance belongs to exactly one shard.
//   - Only that shard's worker thread may call any non-const method.
//   - No internal locking is used; validate() is safe to call from the
//     same thread that owns the state.
//
// LIFECYCLE (live path):
//
//     validate(order)                     -- read-only gate, pre-match
//          |
//          v
//     [matching engine matches order; some qty may fill immediately]
//          |
//          +--> onFill(resting_id, qty) for each trade on a resting order
//          |
//          +--> onOrderResting(order, remaining_qty)  -- ONLY if remaining_qty > 0
//          |
//          +--> onOrderCancelled(order_id, qty) if a resting order is cancelled
//
//   An order that fills in full on entry never rests, so onOrderResting
//   must NOT be called for it. Only the quantity that actually reaches
//   the book is reserved as working exposure.
//
// LIFECYCLE (recovery path):
//
//     restoreAccepted() / restoreFill() / restoreCancelled()
//
//   These replay historical state transitions exactly as they happened.
//   They never call validate() and are not subject to today's risk limits
//   -- a crash-recovery replay must reproduce the past, not re-judge it.
//   If a restore operation detects an impossible state (arithmetic
//   overflow, missing reservation, inconsistent working counter), the
//   engine enters a fail-closed "state corrupt" mode and all future
//   validate()/on* calls are rejected until operator intervention.
//
class RiskEngine {
public:
    RiskEngine() = default;

    // ---------- configuration ----------
    void setMaxOrderSize(uint64_t max_qty) noexcept;
    void setMaxPosition(uint64_t max_pos) noexcept;
    void setPriceBandPct(double pct) noexcept;
    bool setReferencePrice(SymbolId symbol, double price);
    void reservePositions(std::size_t expected_pairs);

    // ---------- side-effect-free validation ----------
    // Read-only. Never mutates any risk state. Safe to call repeatedly.
    RiskRejectReason validate(const Order& order) const noexcept;

    // Read-only. Validates whether replacing `old_order_id` with
    // `new_order` is allowed. Projects working exposure as if the old
    // order's currently reserved quantity were removed and the new
    // order's quantity were added, then checks that projection against
    // the configured limits.
    RiskRejectReason validateReplace(uint64_t old_order_id,
                                     const Order& new_order) const noexcept;

    // ---------- live state transitions (owner thread only) ----------
    // Pre-condition: validate(order) returned NONE.
    // Reserves only `remaining_qty` -- never the full order quantity.
    // Only LIMIT orders may rest; a fully filled order must NOT reach this.
    bool onOrderResting(const Order& order, uint32_t remaining_qty) noexcept;

    // Fill / cancel are addressed by order_id; the engine looks up its
    // internal per-order reservation to determine trader/symbol/side.
    bool onFill(uint64_t order_id, uint32_t fill_qty) noexcept;
    bool onOrderCancelled(uint64_t order_id, uint32_t cancelled_qty) noexcept;

    // ---------- recovery state transitions (startup replay only) ----------
    // Same state changes as the live path, but do NOT validate against
    // current limits. On inconsistency, mark internal state corrupt.
    void restoreAccepted(const Order& order, uint32_t remaining_qty) noexcept;
    void restoreFill(uint64_t order_id, uint32_t fill_qty) noexcept;
    void restoreCancelled(uint64_t order_id, uint32_t cancelled_qty) noexcept;

    // ---------- read-only queries ----------
    int64_t getPosition(uint64_t trader_id, SymbolId symbol) const noexcept;

    // Returns true if recovery detected an unrecoverable inconsistency.
    // A corrupt engine rejects all further validate() calls with
    // RiskRejectReason::STATE_CORRUPT.
    bool isStateCorrupt() const noexcept { return state_corrupt_; }

    // Debug / test only. Verifies bidirectional consistency between
    // RiskEngine's working-exposure counters and the resting orders in
    // `book`:
    //   1. every order tracked by RiskEngine exists in the book with
    //      matching remaining quantity / trader / symbol / side, and
    //   2. the total number of tracked orders equals the book's order count.
    //
    // This uses OrderBook::getOrderById() and OrderBook::getOrderCount().
    // Not for hot-path use.
    bool checkInvariant(const OrderBook& book) const noexcept;

private:
    // ----- keys & state -----
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
            const std::size_t h1 =
                std::hash<uint64_t>{}(key.trader_id);
            const std::size_t h2 =
                std::hash<uint32_t>{}(static_cast<uint32_t>(key.symbol_id));
            return h1 ^
                   (h2 + static_cast<std::size_t>(0x9e3779b9) +
                    (h1 << 6) + (h1 >> 2));
        }
    };

    struct PositionState {
        int64_t  position     = 0;  // executed net position
        uint64_t working_buy  = 0;  // resting BUY quantity
        uint64_t working_sell = 0;  // resting SELL quantity
    };

    // Per-order reservation. Populated by onOrderResting / restoreAccepted;
    // decremented by onFill / onOrderCancelled / restore*; erased when
    // reserved_qty reaches 0.
    struct OrderRiskInfo {
        uint64_t  trader_id    = 0;
        SymbolId  symbol_id    = 0;
        OrderSide side         = OrderSide::BUY;
        uint32_t  reserved_qty = 0;
    };

    static constexpr std::size_t MAX_SYMBOLS = 1'000'000;

    uint64_t max_order_qty_  = 1'000'000;
    uint64_t max_position_   = 10'000'000;

    // Kept as double for compatibility with the current Order API.
    double   price_band_pct_ = 5.0;

    std::vector<double>  ref_prices_;
    std::vector<uint8_t> ref_price_set_;

    std::unordered_map<PositionKey, PositionState, PositionKeyHash> positions_;
    std::unordered_map<uint64_t, OrderRiskInfo>                     order_risk_;

    // Set to true if recovery detects an unrecoverable inconsistency.
    // Once set, all validate() calls return STATE_CORRUPT.
    bool state_corrupt_ = false;

    // ----- helpers -----
    // Shared field-level checks used by validate() / validateReplace().
    // Never touches working-exposure state.
    RiskRejectReason checkOrderFields(const Order& order) const noexcept;

    // Shared implementations used by live and recovery entry points.
    bool applyResting(const Order& order, uint32_t remaining_qty,
                      bool enforce_limits) noexcept;
    bool applyFill(uint64_t order_id, uint32_t fill_qty) noexcept;
    bool applyCancelled(uint64_t order_id, uint32_t cancelled_qty) noexcept;

    void markStateCorrupt() noexcept { state_corrupt_ = true; }

    static bool canAddSigned(int64_t a, int64_t b, int64_t& result) noexcept;
    static bool canSubSigned(int64_t a, int64_t b, int64_t& result) noexcept;
    static bool canAddUnsigned(uint64_t a, uint64_t b,
                               uint64_t& result) noexcept;
};

#endif // RISKENGINE_H