#pragma once
#include "MarketDataPublisher.h"
#include "MarketDataTypes.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class JournalCommand : std::uint8_t {
    NEW_ORDER = 1,
    CANCEL_ORDER = 2,
    FILL = 3
};

struct JournalRecord {
    std::uint64_t sequence;
    JournalCommand command;
    std::vector<std::uint8_t> payload;

    JournalRecord() = default;
    JournalRecord(std::uint64_t seq, JournalCommand cmd, std::vector<std::uint8_t>&& data)
        : sequence(seq), command(cmd), payload(std::move(data)) {}
};

class Journal final : public MarketDataSubscriber {
public:
    explicit Journal(const std::string& file_path, std::size_t queue_capacity = 16384);
    ~Journal() override;

    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    void onEvent(const MarketEvent& event) noexcept override;

    bool flush();
    bool sync();

    bool isHealthy() const noexcept { return healthy_.load(std::memory_order_acquire); }
    std::uint64_t lastWrittenSequence() const noexcept {
        return last_written_sequence_.load(std::memory_order_acquire);
    }

private:
    // Magic bytes for journal header (JNL2)
    static constexpr char kMagic[4] = {'J', 'N', 'L', '2'};
    static constexpr std::uint8_t kVersion = 2;
    static constexpr std::size_t kMaxPayloadSize = 1024;

    void writerThread();
    bool enqueue(JournalRecord&& record) noexcept;
    bool writeRecord(const JournalRecord& record);
    bool writeHeader();
    bool openFile();
    bool validateExistingHeader();
    bool waitUntilWritten(std::uint64_t target);

    std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept;

    static void appendU8(std::vector<std::uint8_t>& out, std::uint8_t value);
    static void appendU32BE(std::vector<std::uint8_t>& out, std::uint32_t value);
    static void appendU64BE(std::vector<std::uint8_t>& out, std::uint64_t value);
    static void appendI64BE(std::vector<std::uint8_t>& out, std::int64_t value);

    std::string file_path_;
    int fd_ = -1;   // raw POSIX file descriptor (no more std::fstream)

    std::atomic<bool> running_{false};
    std::atomic<bool> healthy_{true};
    std::atomic<std::uint64_t> last_written_sequence_{0};

    std::deque<JournalRecord> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable drained_cv_;
    std::size_t queue_capacity_;
    std::mutex file_mutex_;
    std::thread writer_thread_;

    std::uint64_t queued_records_{0};
    std::uint64_t written_records_{0};
};