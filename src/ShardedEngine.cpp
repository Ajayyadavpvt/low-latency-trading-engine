// src/ShardedEngine.cpp
#include "../include/ShardedEngine.h"
#include <stdexcept>

ShardedMatchingEngine::ShardedMatchingEngine(size_t num_shards, size_t buffer_size)
    : symbol_table_(num_shards)   // throws if num_shards == 0
{
    shards_.reserve(num_shards);
    for (size_t i = 0; i < num_shards; ++i) {
        shards_.push_back(std::make_unique<ConcurrentMatchingEngine>(buffer_size));
    }
}

void ShardedMatchingEngine::startAll() {
    symbol_table_.freeze();  // enforce: no registration after workers start
    for (auto& shard : shards_) {
        shard->start();
    }
}

void ShardedMatchingEngine::stopAll() {
    for (auto& shard : shards_) {
        shard->stop();
    }
}

bool ShardedMatchingEngine::submitOrder(const Order& order) {
    size_t shard_idx = symbol_table_.getShardIndex(order.symbol_id);
    if (shard_idx == static_cast<size_t>(-1)) {
        return false;  // invalid symbol
    }
    if (shard_idx >= shards_.size()) {
        return false;
    }
    return shards_[shard_idx]->submitOrder(order);
}

const ConcurrentMatchingEngine& ShardedMatchingEngine::getShard(size_t idx) const {
    if (idx >= shards_.size()) {
        throw std::out_of_range("ShardedMatchingEngine::getShard: index out of range");
    }
    return *shards_[idx];
}