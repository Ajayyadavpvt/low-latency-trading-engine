// tests/test_riskengine.cpp
#include <gtest/gtest.h>

#include "RiskEngine.h"
#include "Order.h"
#include "OrderBook.h"

#include <cmath>
#include <cstdint>
#include <limits>

// Helper: build an Order without constructor validation.
// RiskEngine is responsible for validating these fields.
static Order makeOrder(uint64_t trader_id,
                       uint64_t order_id,
                       OrderSide side,
                       OrderType type,
                       double price,
                       uint32_t quantity,
                       SymbolId symbol = 0) {
    Order o;
    o.trader_id          = trader_id;
    o.order_id           = order_id;
    o.side               = side;
    o.type               = type;
    o.price              = price;
    o.quantity           = quantity;
    o.remaining_quantity = quantity;
    o.symbol_id          = symbol;
    return o;
}

class RiskEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(engine_.setReferencePrice(0, 100.0));
        ASSERT_TRUE(engine_.setReferencePrice(1, 200.0));

        engine_.setMaxOrderSize(1000);
        engine_.setMaxPosition(5000);
        engine_.setPriceBandPct(5.0);
        engine_.reservePositions(1024);
    }

    RiskEngine engine_;

    Order buy(uint64_t trader_id,
              uint64_t order_id,
              uint64_t quantity,
              SymbolId symbol = 0,
              double price = 100.0) {
        return makeOrder(trader_id, order_id,
                         OrderSide::BUY, OrderType::LIMIT,
                         price, static_cast<uint32_t>(quantity), symbol);
    }

    Order sell(uint64_t trader_id,
               uint64_t order_id,
               uint64_t quantity,
               SymbolId symbol = 0,
               double price = 100.0) {
        return makeOrder(trader_id, order_id,
                         OrderSide::SELL, OrderType::LIMIT,
                         price, static_cast<uint32_t>(quantity), symbol);
    }
};

// =========================================================================
// Quantity validation
// =========================================================================

TEST_F(RiskEngineTest, ZeroQuantityRejected) {
    auto order = makeOrder(1, 100, OrderSide::BUY, OrderType::LIMIT,
                           100.0, 0, 0);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::INVALID_QUANTITY);
}

TEST_F(RiskEngineTest, QuantityAtMaximumIsAccepted) {
    engine_.setMaxOrderSize(100);
    auto order = buy(1, 100, 100);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::NONE);
}

TEST_F(RiskEngineTest, QuantityAboveMaximumIsRejected) {
    engine_.setMaxOrderSize(100);
    auto order = buy(1, 100, 101);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::ORDER_TOO_LARGE);
}

// =========================================================================
// Symbol validation
// =========================================================================

TEST_F(RiskEngineTest, UnregisteredSymbolIsRejected) {
    auto order = buy(1, 100, 10, 999);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::INVALID_SYMBOL);
}

// =========================================================================
// Price validation
// =========================================================================

TEST_F(RiskEngineTest, NaNPriceRejected) {
    auto order = makeOrder(1, 100, OrderSide::BUY, OrderType::LIMIT,
                           std::numeric_limits<double>::quiet_NaN(), 10, 0);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::INVALID_PRICE);
}

TEST_F(RiskEngineTest, NegativePriceRejected) {
    auto order = makeOrder(1, 100, OrderSide::BUY, OrderType::LIMIT,
                           -50.0, 10, 0);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::INVALID_PRICE);
}

TEST_F(RiskEngineTest, ZeroPriceRejectedForLimitOrder) {
    auto order = makeOrder(1, 100, OrderSide::BUY, OrderType::LIMIT,
                           0.0, 10, 0);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::INVALID_PRICE);
}

TEST_F(RiskEngineTest, PriceBelowLowerBandIsRejected) {
    auto order = buy(1, 100, 10, 0, 94.99);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::PRICE_BAND_VIOLATION);
}

TEST_F(RiskEngineTest, PriceAboveUpperBandIsRejected) {
    auto order = buy(1, 100, 10, 0, 105.01);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::PRICE_BAND_VIOLATION);
}

TEST_F(RiskEngineTest, PriceAtLowerBandIsAccepted) {
    auto order = buy(1, 100, 10, 0, 95.0);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::NONE);
}

TEST_F(RiskEngineTest, PriceAtUpperBandIsAccepted) {
    auto order = buy(1, 100, 10, 0, 105.0);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::NONE);
}

// =========================================================================
// Position / working exposure
// =========================================================================

TEST_F(RiskEngineTest, PositionLimitRejectedForSameTraderAndSymbol) {
    engine_.setMaxPosition(100);

    auto order1 = buy(1, 100, 100);
    ASSERT_EQ(engine_.validate(order1), RiskRejectReason::NONE);
    ASSERT_TRUE(engine_.onOrderResting(order1, order1.remaining_quantity));

    auto order2 = buy(1, 101, 1);
    EXPECT_EQ(engine_.validate(order2),
              RiskRejectReason::POSITION_LIMIT_EXCEEDED);
}

TEST_F(RiskEngineTest, DifferentTradersHaveIndependentRiskState) {
    engine_.setMaxPosition(100);

    auto trader1_order = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderResting(trader1_order,
                                       trader1_order.remaining_quantity));

    auto trader2_order = buy(2, 101, 100);
    EXPECT_EQ(engine_.validate(trader2_order), RiskRejectReason::NONE);
}

// =========================================================================
// State transitions
// =========================================================================

TEST_F(RiskEngineTest, PartialFillUpdatesPositionCorrectly) {
    auto order = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderResting(order, order.remaining_quantity));
    ASSERT_TRUE(engine_.onFill(order.order_id, 40));
    EXPECT_EQ(engine_.getPosition(1, 0), 40);
    ASSERT_TRUE(engine_.onFill(order.order_id, 20));
    EXPECT_EQ(engine_.getPosition(1, 0), 60);
}

TEST_F(RiskEngineTest, PartialFillThenCancelKeepsExecutedPosition) {
    auto order = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderResting(order, order.remaining_quantity));
    ASSERT_TRUE(engine_.onFill(order.order_id, 40));
    ASSERT_TRUE(engine_.onOrderCancelled(order.order_id, 60));
    EXPECT_EQ(engine_.getPosition(1, 0), 40);
}

TEST_F(RiskEngineTest, SellFillCreatesShortPosition) {
    auto order = sell(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderResting(order, order.remaining_quantity));
    ASSERT_TRUE(engine_.onFill(order.order_id, 100));
    EXPECT_EQ(engine_.getPosition(1, 0), -100);
}

TEST_F(RiskEngineTest, BuyCanReduceShortPosition) {
    auto sell_order = sell(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderResting(sell_order, sell_order.remaining_quantity));
    ASSERT_TRUE(engine_.onFill(sell_order.order_id, 100));

    auto buy_order = buy(1, 101, 50);
    EXPECT_EQ(engine_.validate(buy_order), RiskRejectReason::NONE);
    ASSERT_TRUE(engine_.onOrderResting(buy_order, buy_order.remaining_quantity));
    ASSERT_TRUE(engine_.onFill(buy_order.order_id, 50));
    EXPECT_EQ(engine_.getPosition(1, 0), -50);
}

TEST_F(RiskEngineTest, FillWithoutAcceptedOrderIsRejected) {
    auto order = buy(1, 100, 100);
    EXPECT_FALSE(engine_.onFill(order.order_id, 50));
}

TEST_F(RiskEngineTest, CancelWithoutAcceptedOrderIsRejected) {
    auto order = buy(1, 100, 100);
    EXPECT_FALSE(engine_.onOrderCancelled(order.order_id, 50));
}

TEST_F(RiskEngineTest, FullyCompletedStateClearsPosition) {
    auto buy_order = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderResting(buy_order, buy_order.remaining_quantity));
    ASSERT_TRUE(engine_.onFill(buy_order.order_id, 100));
    EXPECT_EQ(engine_.getPosition(1, 0), 100);

    auto sell_order = sell(1, 101, 100);
    ASSERT_TRUE(engine_.onOrderResting(sell_order, sell_order.remaining_quantity));
    ASSERT_TRUE(engine_.onFill(sell_order.order_id, 100));
    EXPECT_EQ(engine_.getPosition(1, 0), 0);
}

// =========================================================================
// New: validate() is side-effect-free
// =========================================================================

TEST_F(RiskEngineTest, ValidateIsSideEffectFree) {
    auto order = buy(1, 100, 100);

    // Repeated calls must not mutate any state.
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::NONE);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::NONE);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::NONE);

    // Position must remain untouched.
    EXPECT_EQ(engine_.getPosition(1, 0), 0);

    // A subsequent onOrderResting must still take effect normally.
    ASSERT_TRUE(engine_.onOrderResting(order, order.remaining_quantity));

    // Now a second order that would exceed the limit must be rejected,
    // proving the first reservation was actually recorded.
    engine_.setMaxPosition(100);
    EXPECT_EQ(engine_.validate(buy(1, 101, 100)),
              RiskRejectReason::POSITION_LIMIT_EXCEEDED);
}

// =========================================================================
// New: onOrderResting reserves only remaining_qty
// =========================================================================

TEST_F(RiskEngineTest, OnOrderRestingReservesOnlyRemainingQty) {
    engine_.setMaxPosition(100);

    // Order originally 100 shares, but only 40 remain for the book.
    auto order = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderResting(order, 40));

    // 40 + 60 = 100 -> accepted.
    auto fits = buy(1, 101, 60);
    EXPECT_EQ(engine_.validate(fits), RiskRejectReason::NONE);

    // 40 + 61 = 101 -> rejected.
    auto too_much = buy(1, 102, 61);
    EXPECT_EQ(engine_.validate(too_much),
              RiskRejectReason::POSITION_LIMIT_EXCEEDED);
}

// =========================================================================
// New: fully-filled order never rests (no working risk)
// =========================================================================

TEST_F(RiskEngineTest, FullyFilledOrderNeverCallsOnOrderResting) {
    // Simulates a fully-matched incoming order. onOrderResting is NOT
    // called for it. Attempting to fill must therefore be rejected,
    // because no reservation exists.
    auto order = buy(1, 100, 100);
    EXPECT_FALSE(engine_.onFill(order.order_id, 100));
    EXPECT_EQ(engine_.getPosition(1, 0), 0);
}

// =========================================================================
// New: onOrderResting rejects non-LIMIT orders
// =========================================================================

TEST_F(RiskEngineTest, OnOrderRestingRejectsNonLimitOrders) {
    auto market_order = makeOrder(1, 100, OrderSide::BUY,
                                  OrderType::MARKET, 100.0, 50, 0);
    EXPECT_FALSE(engine_.onOrderResting(market_order, 50));

    auto ioc_order = makeOrder(1, 101, OrderSide::BUY,
                               OrderType::IOC, 100.0, 50, 0);
    EXPECT_FALSE(engine_.onOrderResting(ioc_order, 50));

    auto fok_order = makeOrder(1, 102, OrderSide::BUY,
                               OrderType::FOK, 100.0, 50, 0);
    EXPECT_FALSE(engine_.onOrderResting(fok_order, 50));
}

// =========================================================================
// New: validateReplace
// =========================================================================

TEST_F(RiskEngineTest, ValidateReplaceAllowsShrinkingWithinLimit) {
    engine_.setMaxPosition(100);

    auto original = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderResting(original,
                                       original.remaining_quantity));

    // Shrink 100 -> 50.
    auto replacement = buy(1, 100, 50);
    EXPECT_EQ(engine_.validateReplace(100, replacement),
              RiskRejectReason::NONE);
}

TEST_F(RiskEngineTest, ValidateReplaceAllowsGrowthWithinLimit) {
    engine_.setMaxPosition(100);

    auto original = buy(1, 100, 60);
    ASSERT_TRUE(engine_.onOrderResting(original,
                                       original.remaining_quantity));

    // 60 - 60 + 90 = 90 <= 100.
    auto replacement = buy(1, 100, 90);
    EXPECT_EQ(engine_.validateReplace(100, replacement),
              RiskRejectReason::NONE);
}

TEST_F(RiskEngineTest, ValidateReplaceRejectsGrowthBeyondLimit) {
    engine_.setMaxPosition(100);
    // setMaxPosition clamps max_order_qty_ down to 100 as well, so we
    // need to raise the order-size ceiling back up to let the 150-qty
    // replacement reach the position-limit check instead of failing
    // the ORDER_TOO_LARGE field check first.
    engine_.setMaxOrderSize(200);

    auto original = buy(1, 100, 60);
    ASSERT_TRUE(engine_.onOrderResting(original,
                                       original.remaining_quantity));

    // 60 - 60 + 150 = 150 > 100.
    auto replacement = buy(1, 100, 150);
    EXPECT_EQ(engine_.validateReplace(100, replacement),
              RiskRejectReason::POSITION_LIMIT_EXCEEDED);
}

TEST_F(RiskEngineTest, ValidateReplaceRejectsUnknownOrder) {
    auto replacement = buy(1, 999, 50);
    EXPECT_EQ(engine_.validateReplace(999, replacement),
              RiskRejectReason::STATE_VIOLATION);
}

TEST_F(RiskEngineTest, ValidateReplaceRejectsMismatchedTrader) {
    auto original = buy(1, 100, 50);
    ASSERT_TRUE(engine_.onOrderResting(original,
                                       original.remaining_quantity));

    // Same order_id but different trader -> STATE_VIOLATION.
    auto replacement = buy(2, 100, 50);
    EXPECT_EQ(engine_.validateReplace(100, replacement),
              RiskRejectReason::STATE_VIOLATION);
}

// =========================================================================
// New: checkInvariant
// =========================================================================

TEST_F(RiskEngineTest, CheckInvariantPassesForConsistentState) {
    OrderBook book;

    auto order = buy(1, 100, 50);
    ASSERT_TRUE(engine_.onOrderResting(order, 50));
    book.addOrder(order);

    EXPECT_TRUE(engine_.checkInvariant(book));
}

TEST_F(RiskEngineTest, CheckInvariantDetectsBookMissingOrder) {
    OrderBook book;

    auto order = buy(1, 100, 50);
    ASSERT_TRUE(engine_.onOrderResting(order, 50));
    book.addOrder(order);

    ASSERT_TRUE(engine_.checkInvariant(book));

    // Remove from book only (RiskEngine still tracks it) -> mismatch.
    ASSERT_TRUE(book.cancelOrder(order.order_id));
    EXPECT_FALSE(engine_.checkInvariant(book));
}

// =========================================================================
// New: recovery path
// =========================================================================

TEST_F(RiskEngineTest, RestoreMethodsBypassCurrentLimits) {
    engine_.setMaxPosition(10);

    auto historical = buy(1, 100, 100);

    // Historical state is restored even though current limit is 10.
    engine_.restoreAccepted(historical, 100);
    EXPECT_FALSE(engine_.isStateCorrupt());

    // A new order that would exceed the now-tighter limit is rejected.
    auto new_order = buy(1, 101, 1);
    EXPECT_EQ(engine_.validate(new_order),
              RiskRejectReason::POSITION_LIMIT_EXCEEDED);

    engine_.restoreFill(historical.order_id, 100);
    EXPECT_EQ(engine_.getPosition(1, 0), 100);
}

TEST_F(RiskEngineTest, RecoveryRestoresSameStateAsLivePath) {
    // Live path.
    RiskEngine live;
    ASSERT_TRUE(live.setReferencePrice(0, 100.0));
    ASSERT_TRUE(live.setReferencePrice(1, 200.0));
    live.setMaxOrderSize(1000);
    live.setMaxPosition(5000);
    live.setPriceBandPct(5.0);

    auto order = buy(1, 100, 100);
    ASSERT_TRUE(live.onOrderResting(order, 100));
    ASSERT_TRUE(live.onFill(order.order_id, 40));
    ASSERT_TRUE(live.onOrderCancelled(order.order_id, 20));

    const int64_t live_position = live.getPosition(1, 0);

    // Recovery path.
    RiskEngine restored;
    ASSERT_TRUE(restored.setReferencePrice(0, 100.0));
    ASSERT_TRUE(restored.setReferencePrice(1, 200.0));
    restored.setMaxOrderSize(1000);
    restored.setMaxPosition(5000);
    restored.setPriceBandPct(5.0);

    restored.restoreAccepted(order, 100);
    restored.restoreFill(order.order_id, 40);
    restored.restoreCancelled(order.order_id, 20);

    EXPECT_FALSE(restored.isStateCorrupt());
    EXPECT_EQ(restored.getPosition(1, 0), live_position);
}

TEST_F(RiskEngineTest, RestoreFailureMarksEngineCorrupt) {
    // No prior restoreAccepted -> restoreFill cannot find the order.
    engine_.restoreFill(999, 50);

    EXPECT_TRUE(engine_.isStateCorrupt());

    // All future validate() calls return STATE_CORRUPT.
    auto order = buy(1, 100, 10);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::STATE_CORRUPT);
}