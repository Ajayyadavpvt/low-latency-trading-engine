#pragma once

#include "JournalRingBuffer.h"
#include "MarketDataPublisher.h"
#include "MarketDataTypes.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class JournalCommand : std::uint8_t {
    NEW_ORDER   = 1,
    CANCEL_ORDER = 2,
    FILL        = 3
};

// Kept for compatibility with existing Recovery code and any external code
// that already includes Journal.h. Feature F itself no longer constructs
// JournalRecord objects on the producer or writer path.
struct JournalRecord {
    std::uint64_t sequence = 0;
    JournalCommand command = JournalCommand::NEW_ORDER;
    std::vector<std::uint8_t> payload;

    JournalRecord() = default;

    JournalRecord(
        std::uint64_t seq,
        JournalCommand cmd,
        std::vector<std::uint8_t>&& data)
        : sequence(seq)
        , command(cmd)
        , payload(std::move(data)) {
    }
};

class Journal final : public MarketDataSubscriber {
public:
    enum class SyncPolicy {
        PER_RECORD,
        PER_N_RECORDS,
        MANUAL
    };

    static constexpr char kMagic[4] = {'J', 'N', 'L', '2'};

    // Version 3 contains priority_seq in NEW_ORDER.
    static constexpr std::uint8_t kVersion = 3;

    static constexpr std::size_t kMaxPayloadSize =
        JournalRingBuffer::kMaxPayloadSize;

    static constexpr std::size_t kRecordOverhead =
        JournalRingBuffer::kRecordOverhead;

    static constexpr std::size_t kMaxRecordSize =
        JournalRingBuffer::kMaxRecordSize;

    static_assert(
        kMaxRecordSize >=
            kMaxPayloadSize + kRecordOverhead,
        "Journal record buffer is too small");

    explicit Journal(
        const std::string& file_path,
        std::size_t queue_capacity = JournalRingBuffer::kRingCapacity);

    ~Journal() override;

    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    void onEvent(const MarketEvent& event) noexcept override;

    bool flush();
    bool sync();

    void setSyncPolicy(
        SyncPolicy policy,
        std::size_t n = 1);

    bool isHealthy() const noexcept {
        return healthy_.load(
            std::memory_order_acquire);
    }

    std::uint64_t lastWrittenSequence() const noexcept {
        return last_written_sequence_.load(
            std::memory_order_acquire);
    }

private:
    void writerThread();

    bool enqueueSerialized(
        const std::uint8_t* data,
        std::size_t size) noexcept;

    bool writeSerialized(
        const std::uint8_t* data,
        std::size_t size);

    bool validateSerialized(
        const std::uint8_t* data,
        std::size_t size) const noexcept;

    bool writeHeader();

    bool waitUntilWritten(
        std::uint64_t target);

    bool doFdatasync();

    void notifyQueue() noexcept;
    void notifyFlush() noexcept;

    void failStop() noexcept;

    std::uint32_t crc32(
        const std::uint8_t* data,
        std::size_t size) noexcept;

    static std::uint32_t readU32BE(
        const std::uint8_t* data) noexcept;

    static void putU8(
        std::array<std::uint8_t, kMaxRecordSize>& out,
        std::size_t& pos,
        std::uint8_t value);

    static void putU32BE(
        std::array<std::uint8_t, kMaxRecordSize>& out,
        std::size_t& pos,
        std::uint32_t value);

    static void putU64BE(
        std::array<std::uint8_t, kMaxRecordSize>& out,
        std::size_t& pos,
        std::uint64_t value);

    static void putI64BE(
        std::array<std::uint8_t, kMaxRecordSize>& out,
        std::size_t& pos,
        std::int64_t value);

    std::string file_path_;

    int fd_ = -1;

    std::atomic<bool> running_{false};
    std::atomic<bool> healthy_{true};

    std::atomic<std::uint64_t>
        last_written_sequence_{0};

    // Feature F: exactly one SPSC ring per Journal instance / shard.
    JournalRingBuffer ring_;

    // flush_mutex_ is deliberately used for BOTH CV protocols:
    //   queue_cv_ -> "ring contains work / writer should wake"
    //   flush_cv_ -> "written count / failure state changed"
    //
    // Producer state changes happen before notifyQueue().
    // Writer completion state changes happen while holding this mutex.
    // This closes the lost-wakeup window.
    std::mutex flush_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable flush_cv_;

    // Fast-path flag: set true only while a flush waiter is parked.
    // Allows the writer to skip flush_cv_.notify_all() in the common case
    // where nobody is waiting. Read with acquire; written under the
    // flush protocol (set before wait, cleared after).
    std::atomic<bool> flush_waiter_present_{false};

    std::size_t queue_capacity_;

    std::mutex file_mutex_;

    std::thread writer_thread_;

    // Protected by flush_mutex_.
    std::uint64_t queued_records_{0};
    std::uint64_t written_records_{0};

    // Protected by file_mutex_.
    SyncPolicy sync_policy_ = SyncPolicy::MANUAL;
    std::size_t sync_every_n_ = 1;
    std::size_t records_since_sync_ = 0;

    std::thread::id owner_thread_id_{};
};