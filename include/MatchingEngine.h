#pragma once

#include "OrderBook.h"
#include "Order.h"
#include "Trade.h"
#include "MarketDataPublisher.h"

#include <atomic>
#include <cstdint>
#include <vector>

class Journal;

class MatchingEngine {
public:
    enum class EngineState : std::uint8_t {
        RECOVERING = 0,
        READY = 1,
        HALTED = 2
    };

    MatchingEngine() = default;

    std::vector<Trade> processOrder(Order& order);

    bool cancelOrder(uint64_t order_id);

    ReplaceResult replaceOrder(
        uint64_t order_id,
        double new_price,
        uint32_t new_qty);

    bool restoreOrder(
        const Order& order,
        uint32_t remaining_quantity);

    bool restoreCancel(uint64_t order_id);

    bool applyFill(
        uint64_t order_id,
        uint32_t fill_qty);

    bool getOrderById(
        uint64_t order_id,
        Order& out) const;

    double getBestBid() const;
    double getBestAsk() const;
    size_t getOrderCount() const;

    void printBook() const;

    void setSTPPolicy(STPPolicy policy);
    STPPolicy getSTPPolicy() const;

    void setMarketDataPublisher(
        MarketDataPublisher* publisher)
    {
        publisher_ = publisher;
    }

    void setSequenceCounter(
        std::atomic<std::uint64_t>* counter)
    {
        sequence_counter_ = counter;
    }

    void seedSequence(std::uint64_t next_seq)
    {
        if (sequence_counter_) {
            sequence_counter_->store(
                next_seq,
                std::memory_order_release);
        }
    }

    void setJournal(Journal* journal)
    {
        journal_ = journal;
    }

    bool isHealthy() const;

    EngineState state() const noexcept
    {
        return state_.load(std::memory_order_acquire);
    }

    bool isReady() const noexcept
    {
        return state() == EngineState::READY;
    }

    bool isHalted() const noexcept
    {
        return state() == EngineState::HALTED;
    }

    void beginRecovery() noexcept
    {
        EngineState expected = EngineState::READY;

        state_.compare_exchange_strong(
            expected,
            EngineState::RECOVERING,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    bool markReady() noexcept
    {
        EngineState expected = EngineState::RECOVERING;

        return state_.compare_exchange_strong(
            expected,
            EngineState::READY,
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

    void halt() noexcept
    {
        state_.store(
            EngineState::HALTED,
            std::memory_order_release);
    }

    bool tryNextPriority(std::uint64_t& out) noexcept
    {
        std::uint64_t current =
            priority_counter_.load(std::memory_order_relaxed);

        for (;;) {
            if (current == UINT64_MAX) {
                return false;
            }

            const std::uint64_t next = current + 1;

            if (priority_counter_.compare_exchange_weak(
                    current,
                    next,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {

                out = current;
                return true;
            }
        }
    }

    bool seedPrioritySequence(
        std::uint64_t next_seq) noexcept
    {
        std::uint64_t current =
            priority_counter_.load(std::memory_order_acquire);

        for (;;) {
            if (next_seq < current) {
                return false;
            }

            if (next_seq == current) {
                return true;
            }

            if (priority_counter_.compare_exchange_weak(
                    current,
                    next_seq,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {

                return true;
            }
        }
    }

    std::uint64_t peekNextPriority() const noexcept
    {
        return priority_counter_.load(
            std::memory_order_acquire);
    }

private:
    std::uint64_t nextSequence();

    OrderBook book_;

    MarketDataPublisher* publisher_ = nullptr;

    std::atomic<std::uint64_t>* sequence_counter_ = nullptr;

    Journal* journal_ = nullptr;

    std::atomic<EngineState> state_{
        EngineState::READY
    };

    alignas(64)
    std::atomic<std::uint64_t> priority_counter_{0};
};