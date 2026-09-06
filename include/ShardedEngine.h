// include/ShardedEngine.h
#ifndef SHARDEDENGINE_H
#define SHARDEDENGINE_H

#include <vector>
#include <memory>
#include <cstddef>
#include "ConcurrentMatchingEngine.h"
#include "SymbolId.h"
#include "Order.h"

class ShardedMatchingEngine {
public:
    ShardedMatchingEngine(size_t num_shards, size_t buffer_size = 256);

    // No copies — contains threads and unique_ptrs
    ShardedMatchingEngine(const ShardedMatchingEngine&) = delete;
    ShardedMatchingEngine& operator=(const ShardedMatchingEngine&) = delete;

    // Starts all shards and freezes symbol table (no new symbols after start)
    void startAll();

    // Stops all shards (drains pending orders)
    void stopAll();

    // Submit order to shard for its symbol
    bool submitOrder(const Order& order);

    const ConcurrentMatchingEngine& getShard(size_t idx) const;

    SymbolTable& getSymbolTable() { return symbol_table_; }
    const SymbolTable& getSymbolTable() const { return symbol_table_; }

    size_t numShards() const { return shards_.size(); }

private:
    std::vector<std::unique_ptr<ConcurrentMatchingEngine>> shards_;
    SymbolTable symbol_table_;
};

#endif // SHARDEDENGINE_H