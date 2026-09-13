#include "RiskEngine.h"
#include "OrderBook.h"   // for checkInvariant()

#include <cmath>
#include <limits>

namespace {

constexpr int64_t INT64_MAX_VALUE = std::numeric_limits<int64_t>::max();
constexpr int64_t INT64_MIN_VALUE = std::numeric_limits<int64_t>::min();

} // namespace

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void RiskEngine::setMaxOrderSize(uint64_t max_qty) noexcept {
    if (max_qty > static_cast<uint64_t>(INT64_MAX_VALUE)) {
        return;
    }
    max_order_qty_ = max_qty;
}

void RiskEngine::setMaxPosition(uint64_t max_pos) noexcept {
    if (max_pos > static_cast<uint64_t>(INT64_MAX_VALUE)) {
        return;
    }
    max_position_ = max_pos;

    // Preserve the existing configuration invariant:
    // order-size ceiling cannot exceed position ceiling.
    if (max_order_qty_ > max_position_) {
        max_order_qty_ = max_position_;
    }
}

void RiskEngine::setPriceBandPct(double pct) noexcept {
    if (!std::isfinite(pct)) {
        return;
    }
    if (pct < 0.0 || pct >= 100.0) {
        return;
    }
    price_band_pct_ = pct;
}

bool RiskEngine::setReferencePrice(SymbolId symbol, double price) {
    const std::size_t symbol_id = static_cast<std::size_t>(symbol);
    if (symbol_id >= MAX_SYMBOLS) {
        return false;
    }
    if (!std::isfinite(price) || price <= 0.0) {
        return false;
    }

    try {
        if (symbol_id >= ref_prices_.size()) {
            ref_prices_.resize(symbol_id + 1, 0.0);
            ref_price_set_.resize(symbol_id + 1, 0);
        }
    } catch (...) {
        return false;
    }

    ref_prices_[symbol_id]     = price;
    ref_price_set_[symbol_id]  = 1;
    return true;
}

void RiskEngine::reservePositions(std::size_t expected_pairs) {
    positions_.max_load_factor(0.70f);
    positions_.reserve(expected_pairs);

    // order_risk_ typically holds 1-2x the active (trader, symbol) pair
    // count because each pair can have multiple working orders.
    order_risk_.max_load_factor(0.70f);
    order_risk_.reserve(expected_pairs * 2);
}

// ---------------------------------------------------------------------------
// Arithmetic helpers
// ---------------------------------------------------------------------------

bool RiskEngine::canAddSigned(int64_t a, int64_t b, int64_t& result) noexcept {
    if (b > 0 && a > INT64_MAX_VALUE - b) return false;
    if (b < 0 && a < INT64_MIN_VALUE - b) return false;
    result = a + b;
    return true;
}

bool RiskEngine::canSubSigned(int64_t a, int64_t b, int64_t& result) noexcept {
    if (b > 0 && a < INT64_MIN_VALUE + b) return false;
    if (b < 0 && a > INT64_MAX_VALUE + b) return false;
    result = a - b;
    return true;
}

bool RiskEngine::canAddUnsigned(uint64_t a, uint64_t b,
                                uint64_t& result) noexcept {
    if (a > std::numeric_limits<uint64_t>::max() - b) return false;
    result = a + b;
    return true;
}

// ---------------------------------------------------------------------------
// Field-level validation (shared by validate / validateReplace)
// ---------------------------------------------------------------------------

RiskRejectReason RiskEngine::checkOrderFields(const Order& order) const noexcept {
    // Quantity
    if (order.quantity == 0) {
        return RiskRejectReason::INVALID_QUANTITY;
    }
    if (order.quantity > max_order_qty_) {
        return RiskRejectReason::ORDER_TOO_LARGE;
    }

    // Symbol
    const std::size_t symbol_id = static_cast<std::size_t>(order.symbol_id);
    if (symbol_id >= MAX_SYMBOLS ||
        symbol_id >= ref_prices_.size() ||
        ref_price_set_[symbol_id] == 0) {
        return RiskRejectReason::INVALID_SYMBOL;
    }

    // Price (not applicable for MARKET)
    if (order.type != OrderType::MARKET) {
        if (!std::isfinite(order.price) || order.price <= 0.0) {
            return RiskRejectReason::INVALID_PRICE;
        }

        const double ref = ref_prices_[symbol_id];
        if (!std::isfinite(ref) || ref <= 0.0) {
            return RiskRejectReason::INVALID_SYMBOL;
        }

        const double band  = price_band_pct_ / 100.0;
        const double upper = ref * (1.0 + band);
        const double lower = ref * (1.0 - band);

        if (!std::isfinite(upper) || !std::isfinite(lower)) {
            return RiskRejectReason::INVALID_PRICE;
        }
        if (order.price < lower || order.price > upper) {
            return RiskRejectReason::PRICE_BAND_VIOLATION;
        }
    }

    return RiskRejectReason::NONE;
}

// ---------------------------------------------------------------------------
// Side-effect-free validation
// ---------------------------------------------------------------------------

RiskRejectReason RiskEngine::validate(const Order& order) const noexcept {
    if (state_corrupt_) {
        return RiskRejectReason::STATE_CORRUPT;
    }

    const RiskRejectReason field_reason = checkOrderFields(order);
    if (field_reason != RiskRejectReason::NONE) {
        return field_reason;
    }

    const int64_t qty     = static_cast<int64_t>(order.quantity);
    const int64_t max_pos = static_cast<int64_t>(max_position_);

    const PositionKey key{order.trader_id, order.symbol_id};
    const auto it = positions_.find(key);

    const int64_t  position = (it != positions_.end()) ? it->second.position     : 0;
    const uint64_t wbu      = (it != positions_.end()) ? it->second.working_buy  : 0;
    const uint64_t wsu      = (it != positions_.end()) ? it->second.working_sell : 0;

    // NOTE: We deliberately do NOT gate on "wbu > max_position_" here.
    // After recovery, current limits may legitimately be tighter than the
    // historical working exposure that was just restored. The projection
    // check below (position + working + qty vs. max_pos) is the correct
    // place to catch any real overflow or limit violation.

    if (order.side == OrderSide::BUY) {
        int64_t projected = 0;
        if (!canAddSigned(position, static_cast<int64_t>(wbu), projected)) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
        if (!canAddSigned(projected, qty, projected)) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
        if (projected > max_pos) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
    } else {
        int64_t projected = 0;
        if (!canSubSigned(position, static_cast<int64_t>(wsu), projected)) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
        if (!canSubSigned(projected, qty, projected)) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
        if (projected < -max_pos) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
    }

    return RiskRejectReason::NONE;
}

RiskRejectReason RiskEngine::validateReplace(
    uint64_t old_order_id,
    const Order& new_order) const noexcept {

    if (state_corrupt_) {
        return RiskRejectReason::STATE_CORRUPT;
    }

    const RiskRejectReason field_reason = checkOrderFields(new_order);
    if (field_reason != RiskRejectReason::NONE) {
        return field_reason;
    }

    const auto ord_it = order_risk_.find(old_order_id);
    if (ord_it == order_risk_.end()) {
        return RiskRejectReason::STATE_VIOLATION;
    }

    const OrderRiskInfo& old_info = ord_it->second;

    // A replace cannot silently change owner, symbol, or side.
    if (old_info.trader_id != new_order.trader_id ||
        old_info.symbol_id != new_order.symbol_id ||
        old_info.side      != new_order.side) {
        return RiskRejectReason::STATE_VIOLATION;
    }

    const PositionKey key{new_order.trader_id, new_order.symbol_id};
    const auto pos_it = positions_.find(key);

    const int64_t  position = (pos_it != positions_.end()) ? pos_it->second.position : 0;
    const uint64_t current_working =
        (pos_it != positions_.end())
            ? ((new_order.side == OrderSide::BUY)
                   ? pos_it->second.working_buy
                   : pos_it->second.working_sell)
            : 0;

    if (old_info.reserved_qty > current_working) {
        return RiskRejectReason::STATE_CORRUPT;
    }

    uint64_t projected_working = current_working - old_info.reserved_qty;
    if (!canAddUnsigned(projected_working, new_order.quantity,
                        projected_working)) {
        return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
    }

    const int64_t max_pos = static_cast<int64_t>(max_position_);

    if (new_order.side == OrderSide::BUY) {
        int64_t projected = 0;
        if (!canAddSigned(position, static_cast<int64_t>(projected_working),
                          projected)) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
        if (projected > max_pos) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
    } else {
        int64_t projected = 0;
        if (!canSubSigned(position, static_cast<int64_t>(projected_working),
                          projected)) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
        if (projected < -max_pos) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }
    }

    return RiskRejectReason::NONE;
}

// ---------------------------------------------------------------------------
// Shared state transitions
// ---------------------------------------------------------------------------

bool RiskEngine::applyResting(const Order& order,
                              uint32_t remaining_qty,
                              bool enforce_limits) noexcept {

    if (state_corrupt_) {
        return false;
    }
    if (remaining_qty == 0 || remaining_qty > order.quantity) {
        return false;
    }
    // Only LIMIT orders can rest. MARKET / IOC / FOK never reach the book.
    if (order.type != OrderType::LIMIT) {
        return false;
    }
    if (order_risk_.find(order.order_id) != order_risk_.end()) {
        // Already tracked -> reject rather than double-reserve.
        return false;
    }

    const PositionKey key{order.trader_id, order.symbol_id};
    const uint64_t qty = static_cast<uint64_t>(remaining_qty);

    // Check limit BEFORE mutating state.
    const auto pos_it = positions_.find(key);
    const uint64_t current_working =
        (pos_it != positions_.end())
            ? ((order.side == OrderSide::BUY)
                   ? pos_it->second.working_buy
                   : pos_it->second.working_sell)
            : 0;

    uint64_t new_working = 0;
    if (!canAddUnsigned(current_working, qty, new_working)) {
        return false;
    }
    if (enforce_limits && new_working > max_position_) {
        return false;
    }

    // Insert per-order reservation first (smaller, easier to roll back).
    const OrderRiskInfo info{
        order.trader_id,
        order.symbol_id,
        order.side,
        remaining_qty
    };

    try {
        const auto ins = order_risk_.emplace(order.order_id, info);
        if (!ins.second) {
            return false;
        }
    } catch (...) {
        return false;
    }

    // Update or create the position state.
    try {
        if (pos_it == positions_.end()) {
            const auto ins = positions_.emplace(key, PositionState{});
            if (!ins.second) {
                order_risk_.erase(order.order_id);
                return false;
            }
            PositionState& state = ins.first->second;
            if (order.side == OrderSide::BUY) {
                state.working_buy = new_working;
            } else {
                state.working_sell = new_working;
            }
        } else {
            PositionState& state = pos_it->second;
            if (order.side == OrderSide::BUY) {
                state.working_buy = new_working;
            } else {
                state.working_sell = new_working;
            }
        }
    } catch (...) {
        order_risk_.erase(order.order_id);
        return false;
    }

    return true;
}

bool RiskEngine::applyFill(uint64_t order_id, uint32_t fill_qty) noexcept {
    if (state_corrupt_ || fill_qty == 0) {
        return false;
    }

    const auto ord_it = order_risk_.find(order_id);
    if (ord_it == order_risk_.end()) {
        return false;
    }

    OrderRiskInfo& info = ord_it->second;
    if (fill_qty > info.reserved_qty) {
        return false;
    }

    const PositionKey key{info.trader_id, info.symbol_id};
    const auto pos_it = positions_.find(key);
    if (pos_it == positions_.end()) {
        return false;
    }

    PositionState& state = pos_it->second;
    const int64_t signed_fill = static_cast<int64_t>(fill_qty);
    int64_t new_position = 0;

    if (info.side == OrderSide::BUY) {
        if (fill_qty > state.working_buy) {
            return false;
        }
        if (!canAddSigned(state.position, signed_fill, new_position)) {
            return false;
        }
        state.working_buy -= fill_qty;
        state.position     = new_position;
    } else {
        if (fill_qty > state.working_sell) {
            return false;
        }
        if (!canSubSigned(state.position, signed_fill, new_position)) {
            return false;
        }
        state.working_sell -= fill_qty;
        state.position      = new_position;
    }

    info.reserved_qty -= fill_qty;
    if (info.reserved_qty == 0) {
        order_risk_.erase(ord_it);
    }

    if (state.position == 0 && state.working_buy == 0 &&
        state.working_sell == 0) {
        positions_.erase(pos_it);
    }
    return true;
}

bool RiskEngine::applyCancelled(uint64_t order_id,
                                uint32_t cancelled_qty) noexcept {
    if (state_corrupt_ || cancelled_qty == 0) {
        return false;
    }

    const auto ord_it = order_risk_.find(order_id);
    if (ord_it == order_risk_.end()) {
        return false;
    }

    OrderRiskInfo& info = ord_it->second;
    if (cancelled_qty > info.reserved_qty) {
        return false;
    }

    const PositionKey key{info.trader_id, info.symbol_id};
    const auto pos_it = positions_.find(key);
    if (pos_it == positions_.end()) {
        return false;
    }

    PositionState& state = pos_it->second;

    if (info.side == OrderSide::BUY) {
        if (cancelled_qty > state.working_buy) {
            return false;
        }
        state.working_buy -= cancelled_qty;
    } else {
        if (cancelled_qty > state.working_sell) {
            return false;
        }
        state.working_sell -= cancelled_qty;
    }

    info.reserved_qty -= cancelled_qty;
    if (info.reserved_qty == 0) {
        order_risk_.erase(ord_it);
    }

    if (state.position == 0 && state.working_buy == 0 &&
        state.working_sell == 0) {
        positions_.erase(pos_it);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Live path entry points
// ---------------------------------------------------------------------------

bool RiskEngine::onOrderResting(const Order& order,
                                uint32_t remaining_qty) noexcept {
    return applyResting(order, remaining_qty, /*enforce_limits=*/true);
}

bool RiskEngine::onFill(uint64_t order_id, uint32_t fill_qty) noexcept {
    return applyFill(order_id, fill_qty);
}

bool RiskEngine::onOrderCancelled(uint64_t order_id,
                                  uint32_t cancelled_qty) noexcept {
    return applyCancelled(order_id, cancelled_qty);
}

// ---------------------------------------------------------------------------
// Recovery path entry points
// ---------------------------------------------------------------------------
// These do NOT enforce today's limits. Any inconsistency (overflow, missing
// reservation, missing position state) marks the engine corrupt, causing
// all future validate() calls to return STATE_CORRUPT (fail-closed).

void RiskEngine::restoreAccepted(const Order& order,
                                 uint32_t remaining_qty) noexcept {
    const bool ok = applyResting(order, remaining_qty, /*enforce_limits=*/false);
    if (!ok) {
        markStateCorrupt();
    }
}

void RiskEngine::restoreFill(uint64_t order_id, uint32_t fill_qty) noexcept {
    const bool ok = applyFill(order_id, fill_qty);
    if (!ok) {
        markStateCorrupt();
    }
}

void RiskEngine::restoreCancelled(uint64_t order_id,
                                  uint32_t cancelled_qty) noexcept {
    const bool ok = applyCancelled(order_id, cancelled_qty);
    if (!ok) {
        markStateCorrupt();
    }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

int64_t RiskEngine::getPosition(uint64_t trader_id,
                                SymbolId symbol) const noexcept {
    const PositionKey key{trader_id, symbol};
    const auto it = positions_.find(key);
    return (it == positions_.end()) ? 0 : it->second.position;
}

bool RiskEngine::checkInvariant(const OrderBook& book) const noexcept {
    if (state_corrupt_) {
        return false;
    }

    // ---- 1. Count check (pigeonhole) ----
    // If RiskEngine tracks N orders and the book has N orders, and every
    // tracked order exists in the book, then every book order must be
    // tracked (nothing else can fit).
    if (book.getOrderCount() != order_risk_.size()) {
        return false;
    }

    // ---- 2. Per-order field check ----
    for (const auto& entry : order_risk_) {
        const uint64_t order_id = entry.first;
        const OrderRiskInfo& info = entry.second;

        if (info.reserved_qty == 0) {
            return false;   // should have been erased
        }

        Order book_order;
        if (!book.getOrderById(order_id, book_order)) {
            return false;
        }
        if (book_order.order_id          != order_id)          return false;
        if (book_order.trader_id         != info.trader_id)    return false;
        if (book_order.symbol_id         != info.symbol_id)    return false;
        if (book_order.side              != info.side)         return false;
        if (book_order.remaining_quantity != info.reserved_qty) return false;
    }

    // ---- 3. Aggregate working counter check ----
    // Verify that positions_[key].working_buy / working_sell equal the
    // sums computed from the per-order reservations.
    std::unordered_map<PositionKey, uint64_t, PositionKeyHash> derived_buy;
    std::unordered_map<PositionKey, uint64_t, PositionKeyHash> derived_sell;

    try {
        derived_buy.reserve(positions_.size());
        derived_sell.reserve(positions_.size());
    } catch (...) {
        return false;
    }

    for (const auto& entry : order_risk_) {
        const OrderRiskInfo& info = entry.second;
        const PositionKey key{info.trader_id, info.symbol_id};
        if (info.side == OrderSide::BUY) {
            derived_buy[key] += info.reserved_qty;
        } else {
            derived_sell[key] += info.reserved_qty;
        }
    }

    for (const auto& entry : positions_) {
        const PositionKey& key = entry.first;
        const PositionState& state = entry.second;

        const auto it_b = derived_buy.find(key);
        const auto it_s = derived_sell.find(key);
        const uint64_t act_buy =
            (it_b != derived_buy.end()) ? it_b->second : 0;
        const uint64_t act_sell =
            (it_s != derived_sell.end()) ? it_s->second : 0;

        if (state.working_buy  != act_buy)  return false;
        if (state.working_sell != act_sell) return false;
    }

    // ---- 4. No orphan derived keys ----
    for (const auto& entry : derived_buy) {
        if (positions_.find(entry.first) == positions_.end()) {
            return false;
        }
    }
    for (const auto& entry : derived_sell) {
        if (positions_.find(entry.first) == positions_.end()) {
            return false;
        }
    }

    return true;
}