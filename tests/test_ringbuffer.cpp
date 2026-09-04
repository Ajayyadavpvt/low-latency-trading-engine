#include <gtest/gtest.h>
#include "RingBuffer.h"
#include "Order.h"
#include <thread>
#include <vector>

// Test basic push/pop
TEST(RingBufferTest, BasicPushPop) {
    RingBuffer rb(4); // capacity 4, but one slot sacrificed -> 3 usable
    
    Order o1(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    Order o2(2, 200, OrderSide::SELL, OrderType::LIMIT, 101.0, 20);
    
    EXPECT_TRUE(rb.push(o1));
    EXPECT_TRUE(rb.push(o2));
    
    // Use dummy initialization because Order has no default constructor
    Order result(0, 0, OrderSide::BUY, OrderType::MARKET, 0, 0);
    EXPECT_TRUE(rb.pop(result));
    EXPECT_EQ(result.order_id, 1);
    EXPECT_TRUE(rb.pop(result));
    EXPECT_EQ(result.order_id, 2);
    
    // Now empty
    EXPECT_TRUE(rb.isEmpty());
    EXPECT_FALSE(rb.pop(result));
}

// Test full condition
TEST(RingBufferTest, FullBuffer) {
    RingBuffer rb(4); // 3 usable slots
    
    Order o(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    EXPECT_TRUE(rb.push(o));
    EXPECT_TRUE(rb.push(o));
    EXPECT_TRUE(rb.push(o));
    EXPECT_FALSE(rb.push(o)); // 4th should fail (buffer full)
}

// Test multithreaded producer-consumer
TEST(RingBufferTest, ThreadedPushPop) {
    RingBuffer rb(1024); // large capacity
    
    const int NUM_ITEMS = 1000;
    std::vector<Order> consumed;
    consumed.reserve(NUM_ITEMS);
    
    std::thread producer([&rb, NUM_ITEMS]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            Order o(i, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 1);
            while (!rb.push(o)) {
                // spin wait if full
            }
        }
    });
    
    std::thread consumer([&rb, &consumed, NUM_ITEMS]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            Order o(0, 0, OrderSide::BUY, OrderType::MARKET, 0, 0);
            while (!rb.pop(o)) {
                // spin wait if empty
            }
            consumed.push_back(o);
        }
    });
    
    producer.join();
    consumer.join();
    
    ASSERT_EQ(consumed.size(), NUM_ITEMS);
    // Verify order ids are sequential (0 to NUM_ITEMS-1)
    for (int i = 0; i < NUM_ITEMS; ++i) {
        EXPECT_EQ(consumed[i].order_id, i);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}