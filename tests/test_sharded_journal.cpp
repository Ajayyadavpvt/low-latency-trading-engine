#include <gtest/gtest.h>
#include "ShardedJournalManager.h"
#include "MarketDataPublisher.h"
#include <atomic>
#include <cstdio>
#include <filesystem>

class ShardedJournalManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = "test_sharded_journal_dir";
        std::filesystem::remove_all(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string test_dir_;
};

TEST_F(ShardedJournalManagerTest, CreatesJournalsForEachShard) {
    ShardedJournalManager mgr(test_dir_, 4);

    EXPECT_EQ(mgr.numShards(), 4u);

    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_NE(mgr.getJournal(i), nullptr);
        EXPECT_NE(mgr.getSequenceCounter(i), nullptr);
    }

    // Out of range should return nullptr
    EXPECT_EQ(mgr.getJournal(10), nullptr);
    EXPECT_EQ(mgr.getSequenceCounter(10), nullptr);
}

TEST_F(ShardedJournalManagerTest, PerShardIndependentJournals) {
    ShardedJournalManager mgr(test_dir_, 2);

    // Shard 0: process some orders
    {
        auto* journal0 = mgr.getJournal(0);
        auto* seq0 = mgr.getSequenceCounter(0);

        MarketDataPublisher pub0;
        pub0.subscribe(journal0);

        MatchingEngine engine0;
        engine0.setMarketDataPublisher(&pub0);
        engine0.setSequenceCounter(seq0);

        Order buy0(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
        engine0.processOrder(buy0);
        journal0->flush();
    }

    // Shard 1: process different orders
    {
        auto* journal1 = mgr.getJournal(1);
        auto* seq1 = mgr.getSequenceCounter(1);

        MarketDataPublisher pub1;
        pub1.subscribe(journal1);

        MatchingEngine engine1;
        engine1.setMarketDataPublisher(&pub1);
        engine1.setSequenceCounter(seq1);

        Order sell1(1, 200, OrderSide::SELL, OrderType::LIMIT, 101.0, 5, 2);
        engine1.processOrder(sell1);
        journal1->flush();
    }

    // Verify files exist independently
    EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/journal-0.bin"));
    EXPECT_TRUE(std::filesystem::exists(test_dir_ + "/journal-1.bin"));
}

TEST_F(ShardedJournalManagerTest, RecoverSingleShard) {
    // Phase 1: write orders to shard 0
    {
        ShardedJournalManager mgr(test_dir_, 2);

        auto* journal0 = mgr.getJournal(0);
        auto* seq0 = mgr.getSequenceCounter(0);

        MarketDataPublisher pub0;
        pub0.subscribe(journal0);

        MatchingEngine engine0;
        engine0.setMarketDataPublisher(&pub0);
        engine0.setSequenceCounter(seq0);

        Order buy1(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
        engine0.processOrder(buy1);
        Order buy2(2, 200, OrderSide::BUY, OrderType::LIMIT, 99.0, 5, 1);
        engine0.processOrder(buy2);

        journal0->flush();
    }

    // Phase 2: recover into a new manager
    {
        ShardedJournalManager mgr(test_dir_, 2);

        MatchingEngine recovered;
        ASSERT_TRUE(mgr.recoverShard(0, recovered));

        EXPECT_EQ(recovered.getOrderCount(), 2u);
        EXPECT_EQ(recovered.getBestBid(), 100.0);

        // Sequence counter should be seeded
        auto* seq0 = mgr.getSequenceCounter(0);
        EXPECT_GT(seq0->load(), 0u);
    }
}

TEST_F(ShardedJournalManagerTest, MultipleShardsRecoverIndependently) {
    // Phase 1: write to both shards
    {
        ShardedJournalManager mgr(test_dir_, 2);

        {
            auto* journal = mgr.getJournal(0);
            auto* seq = mgr.getSequenceCounter(0);
            MarketDataPublisher pub;
            pub.subscribe(journal);
            MatchingEngine engine;
            engine.setMarketDataPublisher(&pub);
            engine.setSequenceCounter(seq);

            Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, 1);
            engine.processOrder(buy);
            journal->flush();
        }

        {
            auto* journal = mgr.getJournal(1);
            auto* seq = mgr.getSequenceCounter(1);
            MarketDataPublisher pub;
            pub.subscribe(journal);
            MatchingEngine engine;
            engine.setMarketDataPublisher(&pub);
            engine.setSequenceCounter(seq);

            Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 50.0, 20, 2);
            engine.processOrder(buy);
            journal->flush();
        }
    }

    // Phase 2: recover each shard separately
    {
        ShardedJournalManager mgr(test_dir_, 2);

        MatchingEngine recovered0;
        ASSERT_TRUE(mgr.recoverShard(0, recovered0));
        EXPECT_EQ(recovered0.getBestBid(), 100.0);

        MatchingEngine recovered1;
        ASSERT_TRUE(mgr.recoverShard(1, recovered1));
        EXPECT_EQ(recovered1.getBestBid(), 50.0);
    }
}

TEST_F(ShardedJournalManagerTest, SyncAllShards) {
    ShardedJournalManager mgr(test_dir_, 3);

    // Write a bit into each shard
    for (std::size_t i = 0; i < 3; ++i) {
        auto* journal = mgr.getJournal(i);
        auto* seq = mgr.getSequenceCounter(i);
        MarketDataPublisher pub;
        pub.subscribe(journal);
        MatchingEngine engine;
        engine.setMarketDataPublisher(&pub);
        engine.setSequenceCounter(seq);

        Order buy(1, 100, OrderSide::BUY, OrderType::LIMIT,
                  100.0 + i, 10, static_cast<SymbolId>(i));
        engine.processOrder(buy);
    }

    // Should not throw or crash
    mgr.syncAll();

    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(mgr.getJournal(i)->isHealthy());
    }
}