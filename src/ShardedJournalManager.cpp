#include "ShardedJournalManager.h"
#include <filesystem>
#include <stdexcept>

ShardedJournalManager::ShardedJournalManager(const std::string& base_path,
                                              std::size_t num_shards)
    : base_path_(base_path), num_shards_(num_shards) {
    if (num_shards_ == 0) {
        throw std::invalid_argument("ShardedJournalManager: num_shards must be > 0");
    }

    std::filesystem::create_directories(base_path_);

    journals_.reserve(num_shards_);
    sequence_counters_.reserve(num_shards_);

    for (std::size_t i = 0; i < num_shards_; ++i) {
        std::string path = shardJournalPath(i);
        journals_.emplace_back(std::make_unique<Journal>(path, 16384));
        sequence_counters_.emplace_back(
            std::make_unique<std::atomic<std::uint64_t>>(0)
        );
    }
}

std::string ShardedJournalManager::shardJournalPath(std::size_t shard_id) const {
    return base_path_ + "/journal-" + std::to_string(shard_id) + ".bin";
}

Journal* ShardedJournalManager::getJournal(std::size_t shard_id) {
    if (shard_id >= num_shards_) return nullptr;
    return journals_[shard_id].get();
}

std::atomic<std::uint64_t>* ShardedJournalManager::getSequenceCounter(
    std::size_t shard_id) {
    if (shard_id >= num_shards_) return nullptr;
    return sequence_counters_[shard_id].get();
}

bool ShardedJournalManager::recoverShard(std::size_t shard_id,
                                         MatchingEngine& engine) {
    if (shard_id >= num_shards_) return false;

    std::string path = shardJournalPath(shard_id);

    Recovery recovery(path);
    if (!recovery.replay(engine)) {
        return false;
    }

    std::uint64_t last = recovery.lastSequence();
    sequence_counters_[shard_id]->store(last + 1, std::memory_order_release);
    engine.setSequenceCounter(sequence_counters_[shard_id].get());
    engine.seedSequence(last + 1);

    return true;
}

void ShardedJournalManager::syncAll() {
    for (auto& j : journals_) {
        if (j) j->sync();
    }
}