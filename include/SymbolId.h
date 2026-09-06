// include/SymbolId.h
#ifndef SYMBOLID_H
#define SYMBOLID_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>

using SymbolId = uint32_t;

class SymbolTable {
public:
    explicit SymbolTable(size_t num_shards)
        : num_shards_(num_shards), frozen_(false) {
        if (num_shards_ == 0) {
            throw std::invalid_argument("num_shards must be > 0");
        }
    }

    SymbolId registerSymbol(const std::string& symbol) {
        if (frozen_) {
            auto it = symbol_to_id_.find(symbol);
            if (it != symbol_to_id_.end()) return it->second;
            throw std::runtime_error("SymbolTable frozen: cannot register new symbol");
        }

        auto [it, inserted] = symbol_to_id_.try_emplace(symbol, static_cast<SymbolId>(symbols_.size()));
        if (!inserted) {
            return it->second;
        }

        symbols_.push_back(symbol);
        shard_assignment_.push_back(static_cast<size_t>(it->second) % num_shards_);
        return it->second;
    }

    void freeze() { frozen_ = true; }

    size_t getShardIndex(SymbolId id) const {
        if (id >= shard_assignment_.size()) return static_cast<size_t>(-1);
        return shard_assignment_[id];
    }

    std::string getSymbol(SymbolId id) const {
        if (id >= symbols_.size()) return "UNKNOWN";
        return symbols_[id];
    }

    size_t size() const { return symbols_.size(); }
    size_t numShards() const { return num_shards_; }

private:
    size_t num_shards_;
    bool frozen_;
    std::vector<std::string> symbols_;
    std::unordered_map<std::string, SymbolId> symbol_to_id_;
    std::vector<size_t> shard_assignment_;
};

#endif // SYMBOLID_H