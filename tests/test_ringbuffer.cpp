// tests/test_ringbuffer.cpp
#include <gtest/gtest.h>
#include "RingBuffer.h"
#include "Order.h"
#include <thread>
#include <vector>
#include <atomic>

TEST(RingBufferTest, BasicPushPop) {
    RingBuffer rb(4);
    
    Order o1(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    Order o2(2, 200, OrderSide::SELL, OrderType::LIMIT, 101.0, 20);
    
    EXPECT_TRUE(rb.push(o1));
    EXPECT_TRUE(rb.push(o2));
    
    Order result; // default constructor — placeholder, no validation
    EXPECT_TRUE(rb.pop(result));
    EXPECT_EQ(result.order_id, 1);
    EXPECT_TRUE(rb.pop(result));
    EXPECT_EQ(result.order_id, 2);
    
    EXPECT_TRUE(rb.isEmpty());
    EXPECT_FALSE(rb.pop(result));
}

TEST(RingBufferTest, FullBuffer) {
    RingBuffer rb(4);
    
    Order o(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    EXPECT_TRUE(rb.push(o));
    EXPECT_TRUE(rb.push(o));
    EXPECT_TRUE(rb.push(o));
    EXPECT_FALSE(rb.push(o));
}

TEST(RingBufferTest, PushAfterPopWhenFull) {
    RingBuffer rb(4);
    Order o(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10);
    EXPECT_TRUE(rb.push(o));
    EXPECT_TRUE(rb.push(o));
    EXPECT_TRUE(rb.push(o));
    EXPECT_FALSE(rb.push(o));

    Order out; // default placeholder
    EXPECT_TRUE(rb.pop(out));
    EXPECT_TRUE(rb.push(o));
}

TEST(RingBufferTest, WraparoundManyCycles) {
    RingBuffer rb(4);
    Order out; // default placeholder
    for (int cycle = 0; cycle < 10000; ++cycle) {
        Order o(cycle + 1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 1);
        ASSERT_TRUE(rb.push(o));
        ASSERT_TRUE(rb.pop(out));
        EXPECT_EQ(out.order_id, cycle + 1);
    }
}

TEST(RingBufferTest, ThreadedPushPopSmallCapacity) {
    RingBuffer rb(8);
    const int NUM_ITEMS = 200000;
    std::vector<Order> consumed;
    consumed.reserve(NUM_ITEMS);
    std::atomic<bool> stuck{false};

    std::thread producer([&] {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            Order o(i + 1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0 + i, i % 50 + 1);
            int spins = 0;
            while (!rb.push(o)) {
                if (++spins > 100000000) {
                    stuck.store(true);
                    return;
                }
            }
        }
    });

    std::thread consumer([&] {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            Order o; // default placeholder
            int spins = 0;
            while (!rb.pop(o)) {
                if (++spins > 100000000) {
                    stuck.store(true);
                    return;
                }
            }
            consumed.push_back(o);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_FALSE(stuck.load());
    ASSERT_EQ(consumed.size(), NUM_ITEMS);
    for (int i = 0; i < NUM_ITEMS; ++i) {
        EXPECT_EQ(consumed[i].order_id, i + 1);
        EXPECT_EQ(consumed[i].quantity, i % 50 + 1);
        EXPECT_DOUBLE_EQ(consumed[i].price, 100.0 + i);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}