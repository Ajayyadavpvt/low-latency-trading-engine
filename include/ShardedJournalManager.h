#pragma once
#include "Journal.h"
#include "Recovery.h"
#include "MatchingEngine.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ShardedJournalManager {
public:
    ShardedJournalManager(const std::string& base_path, std::size_t num_shards);

    Journal* getJournal(std::size_t shard_id);
    std::atomic<std::uint64_t>* getSequenceCounter(std::size_t shard_id);

    bool recoverShard(std::size_t shard_id, MatchingEngine& engine);

    void syncAll();

    std::size_t numShards() const { return num_shards_; }

private:
    std::string shardJournalPath(std::size_t shard_id) const;

    std::string base_path_;
    std::size_t num_shards_;
    std::vector<std::unique_ptr<Journal>> journals_;
    std::vector<std::unique_ptr<std::atomic<std::uint64_t>>> sequence_counters_;
};