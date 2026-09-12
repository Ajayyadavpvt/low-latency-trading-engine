#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

class JournalRingBuffer {
public:
    static constexpr std::size_t kRingCapacity = 16384;
    static constexpr std::size_t kMaxPayloadSize = 1024;
    static constexpr std::size_t kRecordOverhead = 17;
    static constexpr std::size_t kMaxRecordSize = 1088;

    enum class PushResult : std::uint8_t {
        Ok,
        Full,
        Invalid
    };

    struct RecordView {
        std::array<std::uint8_t, kMaxRecordSize> data{};
        std::size_t size = 0;

        void clear() noexcept {
            size = 0;
        }

        const std::uint8_t* bytes() const noexcept {
            return data.data();
        }
    };

    static_assert(kMaxRecordSize >=
                      kMaxPayloadSize + kRecordOverhead,
                  "kMaxRecordSize must hold the maximum serialized record");

    static_assert(kMaxRecordSize % 64 == 0,
                  "kMaxRecordSize must be cache-line rounded");

    // wrap-safe: head - tail uses unsigned arithmetic; requires
    // capacity << 2^63 so subtraction never wraps incorrectly.
    static_assert(
        static_cast<std::uint64_t>(kRingCapacity) < (1ULL << 63),
        "kRingCapacity must be < 2^63 for wrap-safe subtraction");

    // Configurable capacity. Must be a non-zero power of two.
    // Defaults to kRingCapacity for typical use.
    explicit JournalRingBuffer(
        std::size_t capacity = kRingCapacity);

    ~JournalRingBuffer();

    JournalRingBuffer(const JournalRingBuffer&) = delete;
    JournalRingBuffer& operator=(const JournalRingBuffer&) = delete;

    // Reserved for future extensions (e.g., priority-aware scheduling).
    // Currently unused; ring is FIFO-only.
    PushResult tryPush(
        const std::uint8_t* data,
        std::size_t size,
        std::uint64_t sequence = 0,    // reserved
        std::uint64_t priority = 0,    // reserved
        std::uint8_t record_type = 0)  // reserved
        noexcept;

    bool tryPop(RecordView& view) noexcept;

    bool empty() const noexcept;
    bool full() const noexcept;
    std::size_t approximateSize() const noexcept;
    std::size_t capacity() const noexcept { return capacity_; }

private:
    struct alignas(64) Slot {
        std::uint32_t size = 0;
        std::array<std::uint8_t, kMaxRecordSize> data{};
    };

    static_assert(alignof(Slot) >= 64,
                  "JournalRingBuffer::Slot must be cache-line aligned");

    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};

    std::size_t capacity_;
    std::size_t mask_;
    std::unique_ptr<Slot[]> ring_;
};