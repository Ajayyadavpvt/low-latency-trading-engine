#include <gtest/gtest.h>
#include "Journal.h"
#include "Recovery.h"
#include "MatchingEngine.h"
#include "MarketDataPublisher.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <filesystem>

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

    Order sell(1, 100, OrderSide::SELL, OrderType::LIMIT, 100.0, 10, 1);
    engine.processOrder(sell);
    Order buy(2, 200, OrderSide::BUY, OrderType::LIMIT, 100.0, 4, 1);
    engine.processOrder(buy);
    journal.flush();

    MatchingEngine recovered;
    Recovery recovery(file_path_);
    ASSERT_TRUE(recovery.replay(recovered));
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

    MarketDataPublisher recovery_pub;
    CountingSubscriber counter;
    recovery_pub.subscribe(&counter);

    MatchingEngine recovered;
    recovered.setMarketDataPublisher(&recovery_pub);
    std::atomic<uint64_t> seq2{0};
    recovered.setSequenceCounter(&seq2);

    Recovery recovery(file_path_);
    ASSERT_TRUE(recovery.replay(recovered));

    EXPECT_EQ(counter.count(), 0)
        << "Recovery published events! It must be side-effect free.";
}

// -----------------------------------------------------------------------------
// Restart Sequence Integration Test
// -----------------------------------------------------------------------------
TEST_F(JournalTest, RestartSequenceContinues) {
    {
        MarketDataPublisher pub;
        Journal journal(file_path_, 1024);
        pub.subscribe(&journal);

        MatchingEngine engine;
        engine.setMarketDataPublisher(&pub);
        std::atomic<uint64_t> seq{0};
        engine.setSequenceCounter(&seq);

        Order buy1(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
        engine.processOrder(buy1);
        Order buy2(2, 200, OrderSide::BUY, OrderType::LIMIT, 99.0, 5, 1);
        engine.processOrder(buy2);
        Order buy3(3, 300, OrderSide::BUY, OrderType::LIMIT, 98.0, 7, 1);
        engine.processOrder(buy3);

        journal.flush();
    }

    MatchingEngine recovered;
    Recovery recovery(file_path_);
    ASSERT_TRUE(recovery.replay(recovered));

    std::uint64_t last_seq = recovery.lastSequence();
    EXPECT_GT(last_seq, 0u) << "Expected at least one record to have been journaled";

    std::atomic<uint64_t> new_seq{0};
    recovered.setSequenceCounter(&new_seq);
    recovered.seedSequence(last_seq + 1);

    EXPECT_EQ(new_seq.load(), last_seq + 1)
        << "Sequence should continue from last + 1 after recovery";
}

// -----------------------------------------------------------------------------
// Failure Mode Tests
// -----------------------------------------------------------------------------

// Corrupt CRC: flip a byte in a valid record, recovery should fail
TEST_F(JournalTest, CorruptCRCDetected) {
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
        journal.flush();
    }

    // Corrupt a byte inside the record body
    {
        std::fstream file(file_path_, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());

        // Header = 5 bytes. First record starts at offset 5.
        // Record: length(4) + sequence(8) + command(1) + payload(42) + crc(4)
        // Corrupt byte at offset 10 (inside sequence field)
        file.seekp(10);
        char bad_byte = static_cast<char>(0xFF);
        file.write(&bad_byte, 1);
        file.close();
    }

    MatchingEngine engine;
    Recovery recovery(file_path_);
    EXPECT_FALSE(recovery.replay(engine))
        << "Recovery should fail on corrupt CRC";
}

// Truncated tail: cut the file mid-record, recovery should succeed
// with hadTruncatedTail() == true and all complete records replayed
TEST_F(JournalTest, TruncatedTailHandled) {
    {
        MarketDataPublisher pub;
        Journal journal(file_path_, 1024);
        pub.subscribe(&journal);

        MatchingEngine engine;
        engine.setMarketDataPublisher(&pub);
        std::atomic<uint64_t> seq{0};
        engine.setSequenceCounter(&seq);

        Order buy1(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
        engine.processOrder(buy1);
        Order buy2(2, 200, OrderSide::BUY, OrderType::LIMIT, 99.0, 5, 1);
        engine.processOrder(buy2);
        journal.flush();
    }

    // Truncate the last few bytes (cut last record mid-write)
    {
        std::ifstream in(file_path_, std::ios::binary | std::ios::ate);
        auto size = in.tellg();
        in.close();

        std::filesystem::resize_file(file_path_, static_cast<std::size_t>(size) - 8);
    }

    MatchingEngine engine;
    Recovery recovery(file_path_);
    EXPECT_TRUE(recovery.replay(engine));
    EXPECT_TRUE(recovery.hadTruncatedTail())
        << "Should detect truncated tail";
    EXPECT_GE(recovery.recordsReplayed(), 1u);
}

// -----------------------------------------------------------------------------
// Duplicate Order ID Protection Test
// restoreOrder should reject an order whose ID already exists in the book.
// -----------------------------------------------------------------------------
TEST_F(JournalTest, DuplicateOrderIdRejected) {
    MatchingEngine engine;

    // First restore — should succeed
    Order order1(100, 500, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
    EXPECT_TRUE(engine.restoreOrder(order1, 10));
    EXPECT_EQ(engine.getOrderCount(), 1u);

    // Second restore with SAME order ID — should be rejected
    Order order2(100, 500, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
    EXPECT_FALSE(engine.restoreOrder(order2, 10))
        << "Duplicate order ID should be rejected";

    // Book should still only have 1 order
    EXPECT_EQ(engine.getOrderCount(), 1u);
}

// -----------------------------------------------------------------------------
// Queue Overflow Policy Test
// When queue is full, journal should become unhealthy and reject new records.
// -----------------------------------------------------------------------------
TEST_F(JournalTest, QueueOverflowSetsUnhealthy) {
    // Use tiny queue (capacity = 2) to force overflow easily
    MarketDataPublisher pub;
    Journal journal(file_path_, 2);   // capacity = 2
    pub.subscribe(&journal);

    MatchingEngine engine;
    engine.setMarketDataPublisher(&pub);
    std::atomic<uint64_t> seq{0};
    engine.setSequenceCounter(&seq);

    // Push many orders rapidly — should fill queue and trigger unhealthy
    for (int i = 1; i <= 100; ++i) {
        Order buy(i, 100 + i, OrderSide::BUY, OrderType::LIMIT,
                  100.0 + i * 0.01, 10, 1);
        engine.processOrder(buy);
    }

    // Journal should be unhealthy after overflow
    EXPECT_FALSE(journal.isHealthy())
        << "Journal should become unhealthy after queue overflow";
}