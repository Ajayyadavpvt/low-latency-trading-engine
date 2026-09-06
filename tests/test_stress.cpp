// tests/test_stress.cpp
#include <gtest/gtest.h>
#include "MatchingEngine.h"
#include "ShardedEngine.h"
#include "ConcurrentMatchingEngine.h"
#include "Order.h"
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <iostream>
#include <cstdint>
#include <string>
#include <array>

static double elapsedMs(const std::chrono::steady_clock::time_point& start,
                        const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Stress Test 1: High volume - 1M orders through single-threaded engine
TEST(StressTest, HighVolumeSingleThread) {
    MatchingEngine engine;
    const int NUM_ORDERS = 1000000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_real_distribution<double> price_dist(95.0, 105.0);

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_ORDERS; ++i) {
        OrderSide side = (side_dist(rng) == 0) ? OrderSide::BUY : OrderSide::SELL;
        double price = price_dist(rng);
        uint32_t qty = static_cast<uint32_t>(qty_dist(rng));
        Order order(i + 1, (i % 100) + 1, side, OrderType::LIMIT, price, qty);
        engine.processOrder(order);
    }

    auto end = std::chrono::steady_clock::now();
    double ms = elapsedMs(start, end);
    double throughput = NUM_ORDERS / (ms / 1000.0);

    std::cout << "[Stress] 1M orders: " << ms << " ms, "
              << throughput << " orders/sec, book depth "
              << engine.getOrderCount() << "\n";

    ASSERT_TRUE(engine.getOrderCount() > 0);
}

// Stress Test 2: Multi-producer using ShardedMatchingEngine
TEST(StressTest, MultiProducerSharded) {
    constexpr size_t NUM_SHARDS = 4;
    const int ORDERS_PER_PRODUCER = 100000;
    const size_t TOTAL_ORDERS = NUM_SHARDS * ORDERS_PER_PRODUCER;

    ShardedMatchingEngine engine(NUM_SHARDS, 1024);
    auto& table = engine.getSymbolTable();

    // Use std::array tied to NUM_SHARDS — no hardcoded 4
    std::array<SymbolId, NUM_SHARDS> symbols{};
    for (size_t i = 0; i < NUM_SHARDS; ++i) {
        symbols[i] = table.registerSymbol("SYM" + std::to_string(i));
    }
    table.freeze();
    engine.startAll();

    std::vector<std::thread> producers;
    for (size_t p = 0; p < NUM_SHARDS; ++p) {
        producers.emplace_back([&engine, p, ORDERS_PER_PRODUCER, &symbols]() {
            for (int i = 0; i < ORDERS_PER_PRODUCER; ++i) {
                OrderSide side = (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL;
                double price = 90.0 + (i % 50) * 0.2;
                uint32_t qty = static_cast<uint32_t>((i % 25) + 1);
                Order order(p * ORDERS_PER_PRODUCER + i + 1,
                            p + 1, side, OrderType::LIMIT, price, qty,
                            symbols[p]);
                while (!engine.submitOrder(order)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
        });
    }

    for (auto& t : producers) t.join();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    size_t total_processed = 0;
    while (total_processed < TOTAL_ORDERS &&
           std::chrono::steady_clock::now() < deadline) {
        total_processed = 0;
        for (size_t i = 0; i < NUM_SHARDS; ++i) {
            total_processed += engine.getShard(i).getProcessedCount();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    engine.stopAll();

    ASSERT_EQ(total_processed, TOTAL_ORDERS);
}

// Stress Test 3: Backpressure - full buffer returns false, never silent drop
TEST(StressTest, BackpressureReturnsFalseWhenFull) {
    ConcurrentMatchingEngine engine(4);

    std::vector<Order> orders;
    orders.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        orders.emplace_back(i + 1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 1);
    }

    int accepted = 0;
    int rejected = 0;
    for (const auto& order : orders) {
        if (engine.submitOrder(order)) {
            ++accepted;
        } else {
            ++rejected;
        }
    }

    EXPECT_GT(rejected, 0);
    EXPECT_EQ(accepted + rejected, static_cast<int>(orders.size()));
}