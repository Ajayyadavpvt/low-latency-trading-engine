// tests/test_concurrent_determinism.cpp
#include <gtest/gtest.h>
#include "MatchingEngine.h"
#include "ConcurrentMatchingEngine.h"
#include "Order.h"
#include "Trade.h"
#include <vector>
#include <thread>
#include <chrono>
#include <set>

static std::vector<Order> makeOrders() {
    std::vector<Order> orders;
    orders.emplace_back(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 50);
    orders.emplace_back(2, 200, OrderSide::SELL, OrderType::LIMIT, 100.0, 30);
    orders.emplace_back(3, 300, OrderSide::BUY, OrderType::LIMIT, 101.0, 20);
    orders.emplace_back(4, 400, OrderSide::SELL, OrderType::LIMIT, 99.0, 40);
    orders.emplace_back(5, 500, OrderSide::BUY, OrderType::MARKET, 0, 25);
    orders.emplace_back(6, 600, OrderSide::SELL, OrderType::LIMIT, 102.0, 15);
    orders.emplace_back(7, 700, OrderSide::BUY, OrderType::LIMIT, 98.0, 35);
    return orders;
}

TEST(ConcurrentDeterminismTest, BasicConsistencyWithSequential) {
    // Sequential run (fresh orders)
    std::vector<Order> seq_orders = makeOrders();
    MatchingEngine seq_engine;
    for (auto& order : seq_orders) {
        seq_engine.processOrder(order);
    }

    // Concurrent run (fresh orders, separate vector)
    std::vector<Order> conc_orders = makeOrders();
    ConcurrentMatchingEngine conc_engine(1024);
    conc_engine.start();

    std::thread producer([&]() {
        for (auto& order : conc_orders) {
            while (!conc_engine.submitOrder(order)) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });
    producer.join();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (conc_engine.getProcessedCount() < conc_orders.size() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    conc_engine.stop();

    ASSERT_EQ(conc_engine.getProcessedCount(), conc_orders.size());

    const MatchingEngine& conc_seq = conc_engine.getEngine();
    EXPECT_EQ(conc_seq.getOrderCount(), seq_engine.getOrderCount());
    EXPECT_DOUBLE_EQ(conc_seq.getBestBid(), seq_engine.getBestBid());
    EXPECT_DOUBLE_EQ(conc_seq.getBestAsk(), seq_engine.getBestAsk());
}

TEST(ConcurrentDeterminismTest, UniqueTradeIds) {
    MatchingEngine engine;
    Order buy1(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    engine.processOrder(buy1);
    Order sell1(2, 200, OrderSide::SELL, OrderType::LIMIT, 100.0, 10);
    auto trades1 = engine.processOrder(sell1);
    Order buy2(3, 300, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    auto trades2 = engine.processOrder(buy2);
    Order sell2(4, 400, OrderSide::SELL, OrderType::LIMIT, 100.0, 10);
    auto trades3 = engine.processOrder(sell2);

    std::set<uint64_t> trade_ids;
    for (auto& t : trades1) trade_ids.insert(t.trade_id);
    for (auto& t : trades2) trade_ids.insert(t.trade_id);
    for (auto& t : trades3) trade_ids.insert(t.trade_id);
    EXPECT_EQ(trade_ids.size(), trades1.size() + trades2.size() + trades3.size());
}