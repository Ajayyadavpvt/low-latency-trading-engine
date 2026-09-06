// tests/test_matching.cpp
#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include <stdexcept>
#include "MatchingEngine.h"
#include "Order.h"
#include "Trade.h"

// Test 1: Simple match
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

// Test 3: Price priority
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
    EXPECT_EQ(engine.getOrderCount(), 1);
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 101.0);
}

// Test 6: Empty book no match
TEST(OrderBookTest, EmptyBookNoMatch) {
    MatchingEngine engine;
    Order buy(1, 100, OrderSide::BUY, OrderType::MARKET, 0, 10);
    auto trades = engine.processOrder(buy);
    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(engine.getOrderCount(), 0);
}

// Test 7: Zero quantity rejected
TEST(OrderBookTest, ZeroQuantityRejected) {
    EXPECT_THROW(Order(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 0),
                 std::invalid_argument);
}

// Test 8: Cancel non-existent
TEST(OrderBookTest, CancelNonExistent) {
    MatchingEngine engine;
    EXPECT_FALSE(engine.cancelOrder(999));
}

// Test 9: IOC not added when no match
TEST(OrderBookTest, IOCNotAddedToBook) {
    MatchingEngine engine;
    Order sell(1, 100, OrderSide::SELL, OrderType::LIMIT, 100.0, 10);
    engine.processOrder(sell);

    Order buy_ioc(2, 200, OrderSide::BUY, OrderType::IOC, 99.0, 5);
    auto trades = engine.processOrder(buy_ioc);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(engine.getOrderCount(), 1);
}

// Test 10: FOK partial fill cancelled
TEST(OrderBookTest, FOKPartialFillCancelled) {
    MatchingEngine engine;
    Order sell(1, 100, OrderSide::SELL, OrderType::LIMIT, 100.0, 5);
    engine.processOrder(sell);

    Order buy_fok(2, 200, OrderSide::BUY, OrderType::FOK, 100.0, 10);
    auto trades = engine.processOrder(buy_fok);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(engine.getOrderCount(), 1);
}

// Test 11: Determinism
TEST(OrderBookTest, DeterministicMatching) {
    auto runSequence = [](std::vector<Trade>& trades, MatchingEngine& engine) {
        Order o1(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 50);
        auto t1 = engine.processOrder(o1);
        trades.insert(trades.end(), t1.begin(), t1.end());

        Order o2(2, 200, OrderSide::SELL, OrderType::LIMIT, 100.0, 30);
        auto t2 = engine.processOrder(o2);
        trades.insert(trades.end(), t2.begin(), t2.end());

        Order o3(3, 300, OrderSide::BUY, OrderType::LIMIT, 101.0, 20);
        auto t3 = engine.processOrder(o3);
        trades.insert(trades.end(), t3.begin(), t3.end());

        Order o4(4, 400, OrderSide::SELL, OrderType::LIMIT, 99.0, 40);
        auto t4 = engine.processOrder(o4);
        trades.insert(trades.end(), t4.begin(), t4.end());

        Order o5(5, 500, OrderSide::BUY, OrderType::MARKET, 0, 25);
        auto t5 = engine.processOrder(o5);
        trades.insert(trades.end(), t5.begin(), t5.end());
    };

    MatchingEngine engine1;
    std::vector<Trade> trades1;
    runSequence(trades1, engine1);

    MatchingEngine engine2;
    std::vector<Trade> trades2;
    runSequence(trades2, engine2);

    ASSERT_EQ(trades1.size(), trades2.size());
    ASSERT_GT(trades1.size(), 0u);
    ASSERT_EQ(engine1.getOrderCount(), engine2.getOrderCount());

    for (size_t i = 0; i < trades1.size(); ++i) {
        EXPECT_EQ(trades1[i].trade_id, trades2[i].trade_id);
        EXPECT_EQ(trades1[i].buy_order_id, trades2[i].buy_order_id);
        EXPECT_EQ(trades1[i].sell_order_id, trades2[i].sell_order_id);
        EXPECT_DOUBLE_EQ(trades1[i].price, trades2[i].price);
        EXPECT_EQ(trades1[i].quantity, trades2[i].quantity);
    }
}

// Test 12: IOC partial fill discards remainder
TEST(OrderBookTest, IOCPartialFillDiscardsRemainder) {
    MatchingEngine engine;
    Order sell(1, 100, OrderSide::SELL, OrderType::LIMIT, 100.0, 5);
    engine.processOrder(sell);

    Order buy_ioc(2, 200, OrderSide::BUY, OrderType::IOC, 100.0, 10);
    auto trades = engine.processOrder(buy_ioc);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 5);
    EXPECT_EQ(engine.getOrderCount(), 0);
}

// Test 13: FOK fills across multiple levels
TEST(OrderBookTest, FOKFillsAcrossLevels) {
    MatchingEngine engine;
    Order sell1(1, 100, OrderSide::SELL, OrderType::LIMIT, 100.0, 5);
    engine.processOrder(sell1);

    Order sell2(2, 101, OrderSide::SELL, OrderType::LIMIT, 101.0, 5);
    engine.processOrder(sell2);

    Order buy_fok(3, 300, OrderSide::BUY, OrderType::FOK, 101.0, 10);
    auto trades = engine.processOrder(buy_fok);

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(engine.getOrderCount(), 0);
}

// Test 14: Time priority at equal price
TEST(OrderBookTest, TimePriorityFIFO) {
    MatchingEngine engine;
    Order sell1(1, 100, OrderSide::SELL, OrderType::LIMIT, 100.0, 10);
    engine.processOrder(sell1);

    Order sell2(2, 101, OrderSide::SELL, OrderType::LIMIT, 100.0, 10);
    engine.processOrder(sell2);

    Order buy(3, 300, OrderSide::BUY, OrderType::MARKET, 0, 10);
    auto trades = engine.processOrder(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].sell_order_id, 1);
}

// Test 15: No false self-trade when incoming fills before reaching own order
TEST(OrderBookTest, NoSelfTradeFalsePositive) {
    MatchingEngine engine;
    engine.setSTPPolicy(STPPolicy::CANCEL_NEWEST);

    Order sell1(1, 100, OrderSide::SELL, OrderType::LIMIT, 100.0, 50);
    engine.processOrder(sell1);

    Order sell2(2, 200, OrderSide::SELL, OrderType::LIMIT, 100.0, 10);
    engine.processOrder(sell2);

    Order buy(3, 200, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    auto trades = engine.processOrder(buy);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].sell_order_id, 1);
    EXPECT_EQ(trades[0].quantity, 10);
    EXPECT_EQ(engine.getOrderCount(), 2);
}

// Test 16: Replace order with invalid price rejected
TEST(OrderBookTest, ReplaceInvalidPriceRejected) {
    MatchingEngine engine;
    Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 99.0, 10);
    engine.processOrder(buy);

    auto result = engine.replaceOrder(1, std::nan(""), 5);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(engine.getOrderCount(), 1);
}

// Test 17: Replace with zero quantity cancels
TEST(OrderBookTest, ReplaceZeroQuantityCancels) {
    MatchingEngine engine;
    Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 99.0, 10);
    engine.processOrder(buy);

    auto result = engine.replaceOrder(1, 100.0, 0);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(engine.getOrderCount(), 0);
}

// Test 18: Replace crosses market and generates trade
TEST(OrderBookTest, ReplacePriceChangeGeneratesTrade) {
    MatchingEngine engine;
    Order sell(1, 200, OrderSide::SELL, OrderType::LIMIT, 100.0, 10);
    engine.processOrder(sell);

    Order buy(2, 100, OrderSide::BUY, OrderType::LIMIT, 99.0, 10);
    engine.processOrder(buy);

    auto result = engine.replaceOrder(2, 101.0, 5);
    EXPECT_TRUE(result.success);
    ASSERT_EQ(result.trades.size(), 1);
    EXPECT_EQ(result.trades[0].quantity, 5);
    EXPECT_DOUBLE_EQ(result.trades[0].price, 100.0);
    EXPECT_EQ(result.trades[0].buy_order_id, 2);
    EXPECT_EQ(result.trades[0].sell_order_id, 1);
    EXPECT_EQ(engine.getOrderCount(), 1);
    EXPECT_DOUBLE_EQ(engine.getBestAsk(), 100.0);
}

// Test 19: Replace with non-existent order returns false
TEST(OrderBookTest, ReplaceNonExistentOrder) {
    MatchingEngine engine;
    auto result = engine.replaceOrder(999, 100.0, 10);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.trades.empty());
}