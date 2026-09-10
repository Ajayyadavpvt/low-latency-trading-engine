#include <gtest/gtest.h>
#include "Journal.h"
#include "Recovery.h"
#include "MatchingEngine.h"
#include "MarketDataPublisher.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>

class JournalTest : public ::testing::Test {
protected:
    void SetUp() override {
        file_path_ = "test_journal.bin";
        std::remove(file_path_.c_str());
    }

    void TearDown() override {
        std::remove(file_path_.c_str());
    }

    std::string file_path_;
};

TEST_F(JournalTest, BasicOrderAndRecovery) {
    MarketDataPublisher pub;
    Journal journal(file_path_, 1024);
    pub.subscribe(&journal);

    MatchingEngine engine;
    engine.setMarketDataPublisher(&pub);
    std::atomic<uint64_t> seq{0};
    engine.setSequenceCounter(&seq);

    Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
    engine.processOrder(buy);
    journal.flush();

    MatchingEngine recovered;
    Recovery recovery(file_path_);
    ASSERT_TRUE(recovery.replay(recovered));
    EXPECT_EQ(recovered.getOrderCount(), 1u);
    EXPECT_EQ(recovered.getBestBid(), 100.0);
}

TEST_F(JournalTest, PartialFillRecovery) {
    MarketDataPublisher pub;
    Journal journal(file_path_, 1024);
    pub.subscribe(&journal);

    MatchingEngine engine;
    engine.setMarketDataPublisher(&pub);
    std::atomic<uint64_t> seq{0};
    engine.setSequenceCounter(&seq);

    // Sell 10 @ 100
    Order sell(1, 100, OrderSide::SELL, OrderType::LIMIT, 100.0, 10, 1);
    engine.processOrder(sell);
    // Buy 4 @ 100 (partial fill, sell remaining 6)
    Order buy(2, 200, OrderSide::BUY, OrderType::LIMIT, 100.0, 4, 1);
    engine.processOrder(buy);
    journal.flush();

    MatchingEngine recovered;
    Recovery recovery(file_path_);
    ASSERT_TRUE(recovery.replay(recovered));
    // Only sell remains with 6 remaining
    EXPECT_EQ(recovered.getOrderCount(), 1u);
}

TEST_F(JournalTest, CancelRecovery) {
    MarketDataPublisher pub;
    Journal journal(file_path_, 1024);
    pub.subscribe(&journal);

    MatchingEngine engine;
    engine.setMarketDataPublisher(&pub);
    std::atomic<uint64_t> seq{0};
    engine.setSequenceCounter(&seq);

    Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
    engine.processOrder(buy);
    engine.cancelOrder(1);
    journal.flush();

    MatchingEngine recovered;
    Recovery recovery(file_path_);
    ASSERT_TRUE(recovery.replay(recovered));
    EXPECT_EQ(recovered.getOrderCount(), 0u);
}

TEST_F(JournalTest, EmptyJournal) {
    {
        std::ofstream file(file_path_, std::ios::binary);
        file.write("JNL2", 4);
        std::uint8_t ver = 2;
        file.write(reinterpret_cast<const char*>(&ver), 1);
        file.close();
    }

    MatchingEngine engine;
    Recovery recovery(file_path_);
    EXPECT_TRUE(recovery.replay(engine));
    EXPECT_EQ(recovery.recordsReplayed(), 0u);
}

// -----------------------------------------------------------------------------
// Recovery Isolation Test
// Verify that during recovery no new events are emitted to a publisher.
// -----------------------------------------------------------------------------
class CountingSubscriber : public MarketDataSubscriber {
public:
    void onEvent(const MarketEvent&) noexcept override {
        ++count_;
    }
    int count() const { return count_; }
private:
    int count_ = 0;
};

TEST_F(JournalTest, RecoveryDoesNotPublishEvents) {
    // Phase 1: Create journal with some orders and cancel
    {
        MarketDataPublisher pub;
        Journal journal(file_path_, 1024);
        pub.subscribe(&journal);

        MatchingEngine engine;
        engine.setMarketDataPublisher(&pub);
        std::atomic<uint64_t> seq{0};
        engine.setSequenceCounter(&seq);

        Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
        engine.processOrder(buy);
        Order sell(2, 200, OrderSide::SELL, OrderType::LIMIT, 101.0, 5, 1);
        engine.processOrder(sell);
        engine.cancelOrder(1);

        journal.flush();
    }

    // Phase 2: Recover into a new engine that has its own publisher
    // If recovery incorrectly publishes events, this subscriber will count them.
    MarketDataPublisher recovery_pub;
    CountingSubscriber counter;
    recovery_pub.subscribe(&counter);

    MatchingEngine recovered;
    recovered.setMarketDataPublisher(&recovery_pub);
    std::atomic<uint64_t> seq2{0};
    recovered.setSequenceCounter(&seq2);

    Recovery recovery(file_path_);
    ASSERT_TRUE(recovery.replay(recovered));

    // Recovery should NOT have published any events
    EXPECT_EQ(counter.count(), 0)
        << "Recovery published events! It must be side-effect free.";
}