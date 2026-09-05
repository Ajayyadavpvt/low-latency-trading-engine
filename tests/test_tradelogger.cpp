// tests/test_tradelogger.cpp
#include <gtest/gtest.h>
#include "TradeLogger.h"
#include "Trade.h"
#include <cstdio>
#include <fstream>
#include <iostream>

TEST(TradeLoggerTest, BasicLogging) {
    std::string filename = "test_trades.bin";
    std::remove(filename.c_str());

    {
        TradeLogger logger(filename, 1000);

        Trade t1(1, 10, 20, 100, 200, 100.50, 5);
        EXPECT_TRUE(logger.log(t1));

        Trade t2(2, 11, 21, 101, 201, 101.25, 10);
        EXPECT_TRUE(logger.log(t2));

        logger.stop();
        EXPECT_FALSE(logger.hasError());
    }

    // Verify file size: each trade = 60 bytes, 2 trades = 120 bytes
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(file.is_open());
    std::streamsize size = file.tellg();
    EXPECT_EQ(size, 120);
    file.close();

    std::remove(filename.c_str());
}

TEST(TradeLoggerTest, QueueFullDoesNotCrash) {
    std::string filename = "test_full.bin";
    std::remove(filename.c_str());

    {
        TradeLogger logger(filename, 10);
        Trade t(1, 10, 20, 100, 200, 100.0, 1);

        for (int i = 0; i < 1000; ++i) {
            logger.log(t);  // some will fail when queue is full, but should not crash
        }

        logger.stop();
        EXPECT_FALSE(logger.hasError());
    }

    std::remove(filename.c_str());
}