// include/OrderPool.h
#ifndef ORDERPOOL_H
#define ORDERPOOL_H

#include <vector>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include "Order.h"

// Simple fixed-size memory pool for Order objects.
// Pre-allocates a block of Orders, and hands them out via allocate().
// Freed orders are pushed into a free list for reuse.
//
// Double-free detection is O(1) using an allocated_ bitmap.
//
// NOTE: Not thread-safe — must be used from a single thread.
class OrderPool {
public:
    explicit OrderPool(size_t capacity)
        : storage_(capacity)
        , free_list_(capacity)
        , allocated_(capacity, false)
        , head_(0)
    {
        // Pre-fill free list. allocate() will pop from the tail,
        // giving out indices in descending order. Order of allocation
        // is not semantically important.
        for (size_t i = 0; i < capacity; ++i) {
            free_list_[i] = capacity - 1 - i;
        }
    }

    // Copy/move are deleted because we manage raw pointers into storage_.
    OrderPool(const OrderPool&) = delete;
    OrderPool& operator=(const OrderPool&) = delete;

    // Returns pointer to a free Order slot, or nullptr if pool exhausted.
    Order* allocate() {
        if (head_ >= free_list_.size()) return nullptr;
        size_t idx = free_list_[head_++];
        allocated_[idx] = true;
        return &storage_[idx];
    }

    // Returns an Order slot back to the pool.
    // Throws on double-free or invalid pointer.
    void deallocate(Order* order) {
        if (order == nullptr) return;

        // Guard against empty pool (capacity == 0)
        if (storage_.empty()) {
            throw std::invalid_argument("OrderPool::deallocate: pool is empty");
        }

        // Range check: ensure pointer is within our storage block.
        // Reliable only if caller passes a pointer from allocate().
        if (order < &storage_.front() || order > &storage_.back()) {
            throw std::invalid_argument("OrderPool::deallocate: pointer out of range");
        }

        size_t idx = static_cast<size_t>(order - &storage_[0]);

        // Double-free / not-currently-allocated check
        if (!allocated_[idx]) {
            throw std::runtime_error("OrderPool::deallocate: double free detected");
        }

        allocated_[idx] = false;
        free_list_[--head_] = idx;
    }

    size_t capacity() const { return storage_.size(); }
    size_t available() const { return free_list_.size() - head_; }

private:
    std::vector<Order> storage_;        // all Order slots
    std::vector<size_t> free_list_;     // indices of free slots (stack)
    std::vector<bool> allocated_;       // true if slot currently in use
    size_t head_;                        // next free index to hand out
};

#endif // ORDERPOOL_H