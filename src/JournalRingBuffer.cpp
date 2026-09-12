#include "JournalRingBuffer.h"

#include <cstring>
#include <stdexcept>

JournalRingBuffer::JournalRingBuffer(std::size_t capacity)
    : capacity_(capacity)
    , mask_(capacity - 1)
{
    if (capacity_ == 0 ||
        (capacity_ & (capacity_ - 1)) != 0) {
        throw std::invalid_argument(
            "JournalRingBuffer: capacity must be a non-zero power of two");
    }

    ring_ = std::make_unique<Slot[]>(capacity_);
}

JournalRingBuffer::~JournalRingBuffer() = default;

JournalRingBuffer::PushResult JournalRingBuffer::tryPush(
    const std::uint8_t* data,
    std::size_t size,
    std::uint64_t /*sequence*/,
    std::uint64_t /*priority*/,
    std::uint8_t /*record_type*/) noexcept
{
    if (data == nullptr ||
        size == 0 ||
        size > kMaxRecordSize) {
        return PushResult::Invalid;
    }

    const std::uint64_t head =
        head_.load(std::memory_order_relaxed);

    const std::uint64_t tail =
        tail_.load(std::memory_order_acquire);

    if (head - tail >= capacity_) {
        return PushResult::Full;
    }

    Slot& slot = ring_[static_cast<std::size_t>(head & mask_)];

    std::memcpy(slot.data.data(), data, size);
    slot.size = static_cast<std::uint32_t>(size);

    head_.store(head + 1, std::memory_order_release);

    return PushResult::Ok;
}

bool JournalRingBuffer::tryPop(RecordView& view) noexcept {
    view.clear();

    const std::uint64_t tail =
        tail_.load(std::memory_order_relaxed);

    const std::uint64_t head =
        head_.load(std::memory_order_acquire);

    if (tail == head) {
        return false;
    }

    const Slot& slot =
        ring_[static_cast<std::size_t>(tail & mask_)];

    const std::uint32_t size = slot.size;

    const bool valid =
        size != 0 && size <= kMaxRecordSize;

    if (!valid) {
#ifndef NDEBUG
        assert(size != 0 && size <= kMaxRecordSize);
#endif
        tail_.store(tail + 1, std::memory_order_release);
        return false;
    }

    std::memcpy(view.data.data(), slot.data.data(), size);
    view.size = size;

    tail_.store(tail + 1, std::memory_order_release);

    return true;
}

bool JournalRingBuffer::empty() const noexcept {
    const std::uint64_t head =
        head_.load(std::memory_order_acquire);
    const std::uint64_t tail =
        tail_.load(std::memory_order_acquire);
    return head == tail;
}

bool JournalRingBuffer::full() const noexcept {
    const std::uint64_t head =
        head_.load(std::memory_order_acquire);
    const std::uint64_t tail =
        tail_.load(std::memory_order_acquire);
    return (head - tail) >= capacity_;
}

std::size_t JournalRingBuffer::approximateSize() const noexcept {
    const std::uint64_t head =
        head_.load(std::memory_order_acquire);
    const std::uint64_t tail =
        tail_.load(std::memory_order_acquire);

    const std::uint64_t count = head - tail;

    if (count >= capacity_) {
        return capacity_;
    }

    return static_cast<std::size_t>(count);
}