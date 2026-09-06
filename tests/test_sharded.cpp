// tests/test_sharded.cpp
#include <gtest/gtest.h>
#include "ShardedEngine.h"
#include "Order.h"
#include <thread>
#include <chrono>

TEST(ShardedEngineTest, RoutesOrdersBySymbol) {
    ShardedMatchingEngine engine(2, 256);
    auto& table = engine.getSymbolTable();

    SymbolId btc = table.registerSymbol("BTCUSD");
    SymbolId eth = table.registerSymbol("ETHUSD");
    table.freeze();

    Order btc_buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, btc);
    Order eth_sell(2, 200, OrderSide::SELL, OrderType::LIMIT, 101.0, 5, eth);

    engine.startAll();
    EXPECT_TRUE(engine.submitOrder(btc_buy));
    EXPECT_TRUE(engine.submitOrder(eth_sell));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((engine.getShard(0).getProcessedCount() < 1 ||
            engine.getShard(1).getProcessedCount() < 1) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    engine.stopAll();

    EXPECT_EQ(engine.getShard(0).getProcessedCount(), 1);
    EXPECT_EQ(engine.getShard(1).getProcessedCount(), 1);
    EXPECT_EQ(engine.getShard(0).getEngine().getOrderCount(), 1);
    EXPECT_DOUBLE_EQ(engine.getShard(0).getEngine().getBestBid(), 100.0);
    EXPECT_EQ(engine.getShard(1).getEngine().getOrderCount(), 1);
    EXPECT_DOUBLE_EQ(engine.getShard(1).getEngine().getBestAsk(), 101.0);
}

TEST(ShardedEngineTest, InvalidSymbolRejected) {
    ShardedMatchingEngine engine(2, 256);
    auto& table = engine.getSymbolTable();
    table.freeze();

    Order bad(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 999);
    EXPECT_FALSE(engine.submitOrder(bad));
}