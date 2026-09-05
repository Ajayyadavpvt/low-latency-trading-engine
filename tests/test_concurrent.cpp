// tests/test_concurrent.cpp
#include <gtest/gtest.h>
#include "ConcurrentMatchingEngine.h"
#include "Order.h"
#include <thread>
#include <vector>
#include <chrono>

TEST(ConcurrentEngineTest, BasicProcessing) {
    ConcurrentMatchingEngine engine(256);
    engine.start();

    const int NUM_ORDERS = 1000;

    std::thread producer([&engine, NUM_ORDERS]() {
        for (int i = 0; i < NUM_ORDERS; ++i) {
            // valid non-zero IDs
            Order o(i + 1, 100,
                    (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL,
                    OrderType::LIMIT, 100.0, 1);
            while (!engine.submitOrder(o)) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        }
    });

    producer.join();

    // Poll with timeout — no fixed sleep guess (fixes flaky test)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (engine.getProcessedCount() < NUM_ORDERS &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    engine.stop(); // safe: we confirmed drain (or timed out)

    ASSERT_EQ(engine.getProcessedCount(), NUM_ORDERS)
        << "engine failed to process all submitted orders within timeout";
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}