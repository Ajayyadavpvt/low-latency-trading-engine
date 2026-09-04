#include <gtest/gtest.h>
#include "MatchingEngine.h"
#include "Order.h"

// Test 1: Simple match between buy and sell
TEST(OrderBookTest, SimpleMatch) {
    MatchingEngine engine;
    Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    engine.processOrder(buy);
    
    Order sell(2, 200, OrderSide::SELL, OrderType::LIMIT, 100.0, 10);
    auto trades = engine.processOrder(sell);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 10);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_EQ(trades[0].buy_order_id, 1);
    EXPECT_EQ(trades[0].sell_order_id, 2);
    EXPECT_EQ(engine.getOrderCount(), 0);
}

// Test 2: Partial fill
TEST(OrderBookTest, PartialFill) {
    MatchingEngine engine;
    Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 100);
    engine.processOrder(buy);
    
    Order sell(2, 200, OrderSide::SELL, OrderType::LIMIT, 100.0, 40);
    auto trades = engine.processOrder(sell);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 40);
    EXPECT_EQ(engine.getOrderCount(), 1);
    EXPECT_DOUBLE_EQ(engine.getBestBid(), 100.0);
}

// Test 3: Price priority - best price matches first
TEST(OrderBookTest, PricePriority) {
    MatchingEngine engine;
    Order sell1(1, 100, OrderSide::SELL, OrderType::LIMIT, 101.0, 10);
    engine.processOrder(sell1);
    
    Order sell2(2, 101, OrderSide::SELL, OrderType::LIMIT, 100.0, 10);
    engine.processOrder(sell2);
    
    Order buy(3, 300, OrderSide::BUY, OrderType::MARKET, 0, 10);
    auto trades = engine.processOrder(buy);
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_EQ(trades[0].sell_order_id, 2);
}

// Test 4: Cancel order
TEST(OrderBookTest, CancelOrder) {
    MatchingEngine engine;
    Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    engine.processOrder(buy);
    EXPECT_EQ(engine.getOrderCount(), 1);
    
    bool result = engine.cancelOrder(1);
    EXPECT_TRUE(result);
    EXPECT_EQ(engine.getOrderCount(), 0);
}

// Test 5: Market order fills multiple levels
TEST(OrderBookTest, MarketOrderMultipleLevels) {
    MatchingEngine engine;
    Order sell1(1, 100, OrderSide::SELL, OrderType::LIMIT, 100.0, 5);
    engine.processOrder(sell1);
    
    Order sell2(2, 101, OrderSide::SELL, OrderType::LIMIT, 101.0, 5);
    engine.processOrder(sell2);
    
    Order buy(3, 300, OrderSide::BUY, OrderType::MARKET, 0, 8);
    auto trades = engine.processOrder(buy);
    
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_DOUBLE_EQ(trades[0].price, 100.0);
    EXPECT_EQ(trades[1].quantity, 3);
    EXPECT_DOUBLE_EQ(trades[1].price, 101.0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}