// tests/test_riskengine.cpp
#include <gtest/gtest.h>
#include "RiskEngine.h"
#include "Order.h"
#include <cmath>
#include <limits>

// Helper: build an Order without constructor validation.
// RiskEngine should validate these fields.
static Order makeOrder(uint64_t trader_id,
                       uint64_t order_id,
                       OrderSide side,
                       OrderType type,
                       double price,
                       uint32_t quantity,
                       SymbolId symbol = 0) {
    Order o;                       // default constructor
    o.trader_id = trader_id;
    o.order_id = order_id;
    o.side = side;
    o.type = type;
    o.price = price;
    o.quantity = quantity;
    o.remaining_quantity = quantity;   // assume full for these tests
    o.symbol_id = symbol;
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

    Order buy(uint64_t trader_id, uint64_t order_id,
              uint64_t quantity, SymbolId symbol = 0,
              double price = 100.0) {
        return makeOrder(trader_id, order_id,
                         OrderSide::BUY, OrderType::LIMIT,
                         price, static_cast<uint32_t>(quantity), symbol);
    }

    Order sell(uint64_t trader_id, uint64_t order_id,
               uint64_t quantity, SymbolId symbol = 0,
               double price = 100.0) {
        return makeOrder(trader_id, order_id,
                         OrderSide::SELL, OrderType::LIMIT,
                         price, static_cast<uint32_t>(quantity), symbol);
    }
};

// Quantity validation
TEST_F(RiskEngineTest, ZeroQuantityRejected) {
    auto order = makeOrder(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 0, 0);
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

// Symbol validation
TEST_F(RiskEngineTest, UnregisteredSymbolIsRejected) {
    auto order = buy(1, 100, 10, 999);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::INVALID_SYMBOL);
}

// Price validation
TEST_F(RiskEngineTest, NaNPriceRejected) {
    auto order = makeOrder(1, 100, OrderSide::BUY, OrderType::LIMIT,
                           std::numeric_limits<double>::quiet_NaN(), 10, 0);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::INVALID_PRICE);
}

TEST_F(RiskEngineTest, NegativePriceRejected) {
    auto order = makeOrder(1, 100, OrderSide::BUY, OrderType::LIMIT, -50.0, 10, 0);
    EXPECT_EQ(engine_.validate(order), RiskRejectReason::INVALID_PRICE);
}

TEST_F(RiskEngineTest, ZeroPriceRejectedForLimitOrder) {
    auto order = makeOrder(1, 100, OrderSide::BUY, OrderType::LIMIT, 0.0, 10, 0);
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

// Position / working exposure
TEST_F(RiskEngineTest, PositionLimitRejectedForSameTraderAndSymbol) {
    engine_.setMaxPosition(100);

    auto order1 = buy(1, 100, 100);
    ASSERT_EQ(engine_.validate(order1), RiskRejectReason::NONE);
    ASSERT_TRUE(engine_.onOrderAccepted(order1));

    auto order2 = buy(1, 101, 1);
    EXPECT_EQ(engine_.validate(order2), RiskRejectReason::POSITION_LIMIT_EXCEEDED);
}

TEST_F(RiskEngineTest, DifferentTradersHaveIndependentRiskState) {
    engine_.setMaxPosition(100);

    auto trader1_order = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderAccepted(trader1_order));

    auto trader2_order = buy(2, 101, 100);
    EXPECT_EQ(engine_.validate(trader2_order), RiskRejectReason::NONE);
}

// State transitions
TEST_F(RiskEngineTest, PartialFillUpdatesPositionCorrectly) {
    auto order = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderAccepted(order));
    ASSERT_TRUE(engine_.onFill(order, 40));
    EXPECT_EQ(engine_.getPosition(1, 0), 40);
    ASSERT_TRUE(engine_.onFill(order, 20));
    EXPECT_EQ(engine_.getPosition(1, 0), 60);
}

TEST_F(RiskEngineTest, PartialFillThenCancelKeepsExecutedPosition) {
    auto order = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderAccepted(order));
    ASSERT_TRUE(engine_.onFill(order, 40));
    ASSERT_TRUE(engine_.onOrderCancelled(order, 60));
    EXPECT_EQ(engine_.getPosition(1, 0), 40);
}

TEST_F(RiskEngineTest, SellFillCreatesShortPosition) {
    auto order = sell(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderAccepted(order));
    ASSERT_TRUE(engine_.onFill(order, 100));
    EXPECT_EQ(engine_.getPosition(1, 0), -100);
}

TEST_F(RiskEngineTest, BuyCanReduceShortPosition) {
    auto sell_order = sell(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderAccepted(sell_order));
    ASSERT_TRUE(engine_.onFill(sell_order, 100));

    auto buy_order = buy(1, 101, 50);
    EXPECT_EQ(engine_.validate(buy_order), RiskRejectReason::NONE);
    ASSERT_TRUE(engine_.onOrderAccepted(buy_order));
    ASSERT_TRUE(engine_.onFill(buy_order, 50));
    EXPECT_EQ(engine_.getPosition(1, 0), -50);
}

TEST_F(RiskEngineTest, FillWithoutAcceptedOrderIsRejected) {
    auto order = buy(1, 100, 100);
    EXPECT_FALSE(engine_.onFill(order, 50));
}

TEST_F(RiskEngineTest, CancelWithoutAcceptedOrderIsRejected) {
    auto order = buy(1, 100, 100);
    EXPECT_FALSE(engine_.onOrderCancelled(order, 50));
}

TEST_F(RiskEngineTest, FullyCompletedStateClearsPosition) {
    auto buy_order = buy(1, 100, 100);
    ASSERT_TRUE(engine_.onOrderAccepted(buy_order));
    ASSERT_TRUE(engine_.onFill(buy_order, 100));
    EXPECT_EQ(engine_.getPosition(1, 0), 100);

    auto sell_order = sell(1, 101, 100);
    ASSERT_TRUE(engine_.onOrderAccepted(sell_order));
    ASSERT_TRUE(engine_.onFill(sell_order, 100));
    EXPECT_EQ(engine_.getPosition(1, 0), 0);
}