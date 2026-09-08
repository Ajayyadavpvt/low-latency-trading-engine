#include "RiskEngine.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace {

constexpr int64_t INT64_MAX_VALUE =
    std::numeric_limits<int64_t>::max();

constexpr int64_t INT64_MIN_VALUE =
    std::numeric_limits<int64_t>::min();

} // namespace

void RiskEngine::setMaxOrderSize(uint64_t max_qty) noexcept {
    // All quantities eventually participate in signed position
    // calculations, so keep them representable as int64_t.
    if (max_qty > static_cast<uint64_t>(INT64_MAX_VALUE)) {
        return;
    }

    max_order_qty_ = max_qty;
}

void RiskEngine::setMaxPosition(uint64_t max_pos) noexcept {
    // Position calculations use int64_t.
    if (max_pos > static_cast<uint64_t>(INT64_MAX_VALUE)) {
        return;
    }

    max_position_ = max_pos;

    // Make sure the configured order limit cannot itself exceed
    // the position limit if the caller expects position-based
    // reservation to remain bounded.
    if (max_order_qty_ > max_position_) {
        max_order_qty_ = max_position_;
    }
}

void RiskEngine::setPriceBandPct(double pct) noexcept {
    if (!std::isfinite(pct)) {
        return;
    }

    // 100% would allow a zero lower bound.
    // Negative bands are nonsensical.
    if (pct < 0.0 || pct >= 100.0) {
        return;
    }

    price_band_pct_ = pct;
}

bool RiskEngine::setReferencePrice(SymbolId symbol, double price) {
    const std::size_t symbol_id =
        static_cast<std::size_t>(symbol);

    if (symbol_id >= MAX_SYMBOLS) {
        return false;
    }

    if (!std::isfinite(price) || price <= 0.0) {
        return false;
    }

    if (symbol_id >= ref_prices_.size()) {
        ref_prices_.resize(symbol_id + 1, 0.0);
        ref_price_set_.resize(symbol_id + 1, 0);
    }

    ref_prices_[symbol_id] = price;
    ref_price_set_[symbol_id] = 1;

    return true;
}

void RiskEngine::reservePositions(std::size_t expected_pairs) {
    // Set load factor BEFORE reserve so reserve() calculates the
    // bucket count using the desired load factor.
    positions_.max_load_factor(0.70f);
    positions_.reserve(expected_pairs);
}

bool RiskEngine::canAddSigned(
    int64_t a,
    int64_t b,
    int64_t& result) noexcept {

    if (b > 0 && a > INT64_MAX_VALUE - b) {
        return false;
    }

    if (b < 0 && a < INT64_MIN_VALUE - b) {
        return false;
    }

    result = a + b;
    return true;
}

bool RiskEngine::canSubSigned(
    int64_t a,
    int64_t b,
    int64_t& result) noexcept {

    if (b > 0 && a < INT64_MIN_VALUE + b) {
        return false;
    }

    if (b < 0 && a > INT64_MAX_VALUE + b) {
        return false;
    }

    result = a - b;
    return true;
}

bool RiskEngine::canAddUnsigned(
    uint64_t a,
    uint64_t b,
    uint64_t& result) noexcept {

    if (a > std::numeric_limits<uint64_t>::max() - b) {
        return false;
    }

    result = a + b;
    return true;
}

RiskRejectReason RiskEngine::validate(
    const Order& order) const noexcept {

    // ------------------------------------------------------------
    // 1. Quantity
    // ------------------------------------------------------------

    if (order.quantity == 0) {
        return RiskRejectReason::INVALID_QUANTITY;
    }

    if (order.quantity > max_order_qty_) {
        return RiskRejectReason::ORDER_TOO_LARGE;
    }

    // max_order_qty_ was configured to fit in int64_t.
    const int64_t qty =
        static_cast<int64_t>(order.quantity);

    const int64_t max_pos =
        static_cast<int64_t>(max_position_);

    // ------------------------------------------------------------
    // 2. Symbol
    // ------------------------------------------------------------

    const std::size_t symbol_id =
        static_cast<std::size_t>(order.symbol_id);

    if (symbol_id >= MAX_SYMBOLS) {
        return RiskRejectReason::INVALID_SYMBOL;
    }

    if (symbol_id >= ref_prices_.size()) {
        return RiskRejectReason::INVALID_SYMBOL;
    }

    if (ref_price_set_[symbol_id] == 0) {
        return RiskRejectReason::INVALID_SYMBOL;
    }

    // ------------------------------------------------------------
    // 3. Price validation
    // ------------------------------------------------------------

    if (order.type != OrderType::MARKET) {

        if (!std::isfinite(order.price) ||
            order.price <= 0.0) {
            return RiskRejectReason::INVALID_PRICE;
        }

        const double ref = ref_prices_[symbol_id];

        if (!std::isfinite(ref) || ref <= 0.0) {
            return RiskRejectReason::INVALID_SYMBOL;
        }

        const double band =
            price_band_pct_ / 100.0;

        const double upper =
            ref * (1.0 + band);

        const double lower =
            ref * (1.0 - band);

        if (!std::isfinite(upper) ||
            !std::isfinite(lower)) {
            return RiskRejectReason::INVALID_PRICE;
        }

        if (order.price < lower ||
            order.price > upper) {
            return RiskRejectReason::PRICE_BAND_VIOLATION;
        }
    }

    // ------------------------------------------------------------
    // 4. Position / working exposure
    // ------------------------------------------------------------

    PositionKey key{
        order.trader_id,
        order.symbol_id
    };

    const auto it = positions_.find(key);

    const int64_t position =
        (it != positions_.end())
            ? it->second.position
            : 0;

    const uint64_t working_buy_u =
        (it != positions_.end())
            ? it->second.working_buy
            : 0;

    const uint64_t working_sell_u =
        (it != positions_.end())
            ? it->second.working_sell
            : 0;

    // These should always be <= max_position_ because accepted
    // working orders are bounded by the risk engine.
    if (working_buy_u > max_position_ ||
        working_sell_u > max_position_) {
        return RiskRejectReason::STATE_VIOLATION;
    }

    const int64_t working_buy =
        static_cast<int64_t>(working_buy_u);

    const int64_t working_sell =
        static_cast<int64_t>(working_sell_u);

    // ------------------------------------------------------------
    // BUY:
    //
    // projected position =
    //     current position
    //   + existing working buys
    //   + new order
    //
    // Must remain <= max_position.
    // ------------------------------------------------------------

    if (order.side == OrderSide::BUY) {

        int64_t projected = 0;

        if (!canAddSigned(position, working_buy, projected)) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }

        if (!canAddSigned(projected, qty, projected)) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }

        if (projected > max_pos) {
            return RiskRejectReason::POSITION_LIMIT_EXCEEDED;
        }

    } else {

        // --------------------------------------------------------
        // SELL:
        //
        // projected position =
        //     current position
        //   - existing working sells
        //   - new order
        //
        // Must remain >= -max_position.
        // --------------------------------------------------------

        int64_t projected = 0;

        if (!canSubSigned(position, working_sell, projected)) {
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

bool RiskEngine::onOrderAccepted(
    const Order& order) noexcept {

    // This function assumes validate(order) returned NONE.
    //
    // Keeping this assertion in debug builds makes accidental
    // lifecycle misuse easier to detect.
    const RiskRejectReason reason = validate(order);

    if (reason != RiskRejectReason::NONE) {
        return false;
    }

    PositionKey key{
        order.trader_id,
        order.symbol_id
    };

    auto it = positions_.find(key);

    if (it == positions_.end()) {

        // This is the first position state for this
        // trader/symbol pair.
        //
        // unordered_map may allocate here.
        // This is why the current implementation is NOT yet
        // strictly zero-allocation.
        auto result = positions_.emplace(
            key,
            PositionState{}
        );

        if (!result.second) {
            return false;
        }

        it = result.first;
    }

    PositionState& state = it->second;

    if (order.side == OrderSide::BUY) {

        uint64_t new_working = 0;

        if (!canAddUnsigned(
                state.working_buy,
                order.quantity,
                new_working)) {
            return false;
        }

        if (new_working > max_position_) {
            return false;
        }

        state.working_buy = new_working;

    } else {

        uint64_t new_working = 0;

        if (!canAddUnsigned(
                state.working_sell,
                order.quantity,
                new_working)) {
            return false;
        }

        if (new_working > max_position_) {
            return false;
        }

        state.working_sell = new_working;
    }

    return true;
}

bool RiskEngine::onFill(
    const Order& order,
    uint64_t fill_qty) noexcept {

    if (fill_qty == 0) {
        return false;
    }

    if (fill_qty > max_position_) {
        return false;
    }

    PositionKey key{
        order.trader_id,
        order.symbol_id
    };

    auto it = positions_.find(key);

    // A fill must correspond to an existing accepted/working order.
    // Never create a position entry merely because a fill arrived.
    if (it == positions_.end()) {
        return false;
    }

    PositionState& state = it->second;

    if (order.side == OrderSide::BUY) {

        if (fill_qty > state.working_buy) {
            return false;
        }

        const int64_t signed_fill =
            static_cast<int64_t>(fill_qty);

        int64_t new_position = 0;

        if (!canAddSigned(
                state.position,
                signed_fill,
                new_position)) {
            return false;
        }

        // State transition happens only after every check succeeds.
        state.working_buy -= fill_qty;
        state.position = new_position;

    } else {

        if (fill_qty > state.working_sell) {
            return false;
        }

        const int64_t signed_fill =
            static_cast<int64_t>(fill_qty);

        int64_t new_position = 0;

        if (!canSubSigned(
                state.position,
                signed_fill,
                new_position)) {
            return false;
        }

        state.working_sell -= fill_qty;
        state.position = new_position;
    }

    // Fully inactive trader/symbol state can be removed.
    if (state.position == 0 &&
        state.working_buy == 0 &&
        state.working_sell == 0) {

        positions_.erase(it);
    }

    return true;
}

bool RiskEngine::onOrderCancelled(
    const Order& order,
    uint64_t cancelled_qty) noexcept {

    if (cancelled_qty == 0) {
        return false;
    }

    PositionKey key{
        order.trader_id,
        order.symbol_id
    };

    auto it = positions_.find(key);

    // A cancellation must correspond to existing risk state.
    if (it == positions_.end()) {
        return false;
    }

    PositionState& state = it->second;

    if (order.side == OrderSide::BUY) {

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

    // If nothing remains associated with this trader/symbol,
    // release the hash-table entry.
    if (state.position == 0 &&
        state.working_buy == 0 &&
        state.working_sell == 0) {

        positions_.erase(it);
    }

    return true;
}

int64_t RiskEngine::getPosition(
    uint64_t trader_id,
    SymbolId symbol) const noexcept {

    PositionKey key{
        trader_id,
        symbol
    };

    const auto it = positions_.find(key);

    if (it == positions_.end()) {
        return 0;
    }

    return it->second.position;
}