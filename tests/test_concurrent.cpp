// tests/test_concurrent.cpp
#include <gtest/gtest.h>
#include "ConcurrentMatchingEngine.h"
#include <thread>
#include <vector>
#include <chrono>

// Test basic concurrent order processing
TEST(ConcurrentEngineTest, BasicProcessing) {
    ConcurrentMatchingEngine engine(256);
    engine.start();
    
    const int NUM_ORDERS = 1000;
    std::vector<Order> orders;
    orders.reserve(NUM_ORDERS);
    
    // Producer thread
    std::thread producer([&engine, NUM_ORDERS]() {
        for (int i = 0; i < NUM_ORDERS; ++i) {
            Order o(i, 100, 
                    (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL,
                    OrderType::LIMIT, 100.0, 1);
            while (!engine.submitOrder(o)) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        }
    });
    
    producer.join();
    
    // Wait for consumer to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    engine.stop();
    
    EXPECT_EQ(engine.getProcessedCount(), NUM_ORDERS);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}