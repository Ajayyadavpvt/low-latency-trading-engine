// include/OrderQueue.h
#ifndef ORDERQUEUE_H
#define ORDERQUEUE_H

#include <vector>
#include <cstddef>
#include <algorithm>
#include <utility>
#include <stdexcept>
#include <cassert>
#include "Order.h"
#include "OrderPool.h"

// Vector-backed queue backed by OrderPool.
// Orders are allocated from a pre-allocated pool (no per-order dynamic allocation).
class OrderQueue {
public:
    explicit OrderQueue(OrderPool* pool)
        : pool_(pool)
    {
        if (!pool_) {
            throw std::invalid_argument("OrderQueue: pool cannot be null");
        }
        pointers_.reserve(64);
    }

    // No copying — would lead to double ownership of pointers
    OrderQueue(const OrderQueue&) = delete;
    OrderQueue& operator=(const OrderQueue&) = delete;

    // Move is safe
    OrderQueue(OrderQueue&& other) noexcept
        : pool_(other.pool_)
        , pointers_(std::move(other.pointers_))
        , head_(other.head_)
        , tail_(other.tail_)
    {
        other.pool_ = nullptr;
        other.head_ = other.tail_ = 0;
    }

    OrderQueue& operator=(OrderQueue&& other) noexcept {
        if (this != &other) {
            pool_ = other.pool_;
            pointers_ = std::move(other.pointers_);
            head_ = other.head_;
            tail_ = other.tail_;
            other.pool_ = nullptr;
            other.head_ = other.tail_ = 0;
        }
        return *this;
    }

    void push_back(const Order& order) {
        // Grow pointer array if needed
        if (tail_ == pointers_.size()) {
            if (head_ > 0) {
                // Reclaim dead space at front
                std::move(pointers_.begin() + head_, pointers_.begin() + tail_, pointers_.begin());
                tail_ -= head_;
                head_ = 0;
            }
            if (tail_ == pointers_.size()) {
                pointers_.resize(std::max<size_t>(pointers_.size() * 2, 16), nullptr);
            }
        }

        Order* ptr = pool_->allocate();
        if (!ptr) {
            throw std::runtime_error("OrderQueue: OrderPool exhausted");
        }
        *ptr = order;
        pointers_[tail_++] = ptr;
    }

    Order& front() {
        assert(!empty() && "OrderQueue::front() on empty queue");
        return *pointers_[head_];
    }

    const Order& front() const {
        assert(!empty() && "OrderQueue::front() on empty queue");
        return *pointers_[head_];
    }

    void pop_front() {
        if (empty()) return;
        pool_->deallocate(pointers_[head_]);
        pointers_[head_] = nullptr;
        ++head_;
        if (head_ == tail_) {
            head_ = tail_ = 0;
        }
    }

    bool empty() const { return head_ == tail_; }
    size_t size() const { return tail_ - head_; }

    Order& at(size_t i) {
        assert(head_ + i < tail_ && "OrderQueue::at() out of range");
        return *pointers_[head_ + i];
    }

    const Order& at(size_t i) const {
        assert(head_ + i < tail_ && "OrderQueue::at() out of range");
        return *pointers_[head_ + i];
    }

    void erase_at(size_t i) {
        assert(head_ + i < tail_ && "OrderQueue::erase_at() out of range");
        pool_->deallocate(pointers_[head_ + i]);

        for (size_t j = head_ + i + 1; j < tail_; ++j) {
            pointers_[j - 1] = pointers_[j];
        }
        pointers_[--tail_] = nullptr;

        if (head_ == tail_) {
            head_ = tail_ = 0;
        }
    }

private:
    OrderPool* pool_;
    std::vector<Order*> pointers_;
    size_t head_ = 0;
    size_t tail_ = 0;
};

#endif // ORDERQUEUE_H