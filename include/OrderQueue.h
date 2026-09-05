// include/OrderQueue.h
#ifndef ORDERQUEUE_H
#define ORDERQUEUE_H

#include <vector>
#include <cstddef>
#include <algorithm>
#include <cassert>
#include "Order.h"

// Vector-backed queue with lazy head index.
// O(1) push_back (amortized), O(1) pop_front, O(1) front.
// Contiguous memory, no per-chunk overhead. Capacity-triggered compaction.
class OrderQueue {
public:
    void push_back(const Order& order) {
        if (tail_ == buffer_.size()) {
            if (head_ > 0) {
                // Reclaim dead space at front before growing
                std::move(buffer_.begin() + head_, buffer_.begin() + tail_, buffer_.begin());
                tail_ -= head_;
                head_ = 0;
            }
            if (tail_ == buffer_.size()) {
                buffer_.resize(std::max<size_t>(buffer_.size() * 2, 16));
            }
        }
        buffer_[tail_++] = order;
    }

    Order& front() {
        assert(!empty() && "OrderQueue::front() on empty queue");
        return buffer_[head_];
    }

    const Order& front() const {
        assert(!empty() && "OrderQueue::front() on empty queue");
        return buffer_[head_];
    }

    void pop_front() {
        if (empty()) return;  // safe no-op
        ++head_;
        if (head_ == tail_) head_ = tail_ = 0; // fully drained: O(1) reset
    }

    bool empty() const { return head_ == tail_; }
    size_t size() const { return tail_ - head_; }

    // Iterator support for scan/print
    auto begin() { return buffer_.begin() + head_; }
    auto end() { return buffer_.begin() + tail_; }
    auto begin() const { return buffer_.begin() + head_; }
    auto end() const { return buffer_.begin() + tail_; }

    // Real removal for cancelOrder
    auto erase(typename std::vector<Order>::iterator it) {
        assert(it >= buffer_.begin() + head_ && it < buffer_.begin() + tail_);
        auto next = it;
        std::move(it + 1, buffer_.begin() + tail_, it);
        --tail_;
        if (head_ == tail_) head_ = tail_ = 0; // reset if empty
        return next;
    }

private:
    std::vector<Order> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;
};

#endif // ORDERQUEUE_H