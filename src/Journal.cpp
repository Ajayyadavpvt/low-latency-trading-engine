#include "Journal.h"

#include <array>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
    #include <fcntl.h>
    #include <io.h>
    #include <sys/stat.h>

    static int j_open(const char* path, int flags, int mode) {
        return _open(path, flags, mode);
    }
    static int j_close(int fd) { return _close(fd); }
    static int j_write(int fd, const void* data, unsigned int size) {
        return _write(fd, data, size);
    }
    static long long j_lseek(int fd, long long offset, int whence) {
        return _lseeki64(fd, offset, whence);
    }
    static int j_sync(int /*fd*/) { return 0; }

    static constexpr int JOURNAL_OPEN_FLAGS =
        _O_RDWR | _O_CREAT | _O_APPEND | _O_BINARY;
    static constexpr int JOURNAL_OPEN_MODE = _S_IREAD | _S_IWRITE;
#else
    #include <fcntl.h>
    #include <unistd.h>

    static int j_open(const char* path, int flags, int mode) {
        return ::open(path, flags, mode);
    }
    static int j_close(int fd) { return ::close(fd); }
    static ssize_t j_write(int fd, const void* data, size_t size) {
        return ::write(fd, data, size);
    }
    static off_t j_lseek(int fd, off_t offset, int whence) {
        return ::lseek(fd, offset, whence);
    }
    static int j_sync(int fd) { return ::fdatasync(fd); }

    static constexpr int JOURNAL_OPEN_FLAGS = O_RDWR | O_CREAT | O_APPEND;
    static constexpr int JOURNAL_OPEN_MODE = 0644;
#endif

namespace {

std::uint32_t g_crc_table[256];
std::once_flag g_crc_once;

void initializeCRC32() {
    std::call_once(g_crc_once, [] {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                if (crc & 1U) crc = (crc >> 1U) ^ 0xEDB88320U;
                else crc >>= 1U;
            }
            g_crc_table[i] = crc;
        }
    });
}

bool writeAll(int fd, const std::uint8_t* data, std::size_t size) noexcept {
    std::size_t written = 0;
    while (written < size) {
#ifdef _WIN32
        const unsigned int chunk = static_cast<unsigned int>(
            (size - written) > 0x7ffff000U ? 0x7ffff000U : (size - written));
        const int n = j_write(fd, data + written, chunk);
#else
        const ssize_t n = j_write(fd, data + written, size - written);
#endif
        if (n < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            return false;
        }
        if (n == 0) return false;
        written += static_cast<std::size_t>(n);
    }
    return true;
}

bool readAll(int fd, void* data, std::size_t size) noexcept {
#ifdef _WIN32
    std::size_t read_bytes = 0;
    while (read_bytes < size) {
        const unsigned int chunk = static_cast<unsigned int>(
            (size - read_bytes) > 0x7ffff000U ? 0x7ffff000U : (size - read_bytes));
        const int n = _read(fd, static_cast<char*>(data) + read_bytes, chunk);
        if (n < 0) return false;
        if (n == 0) return false;
        read_bytes += static_cast<std::size_t>(n);
    }
    return true;
#else
    std::size_t read_bytes = 0;
    while (read_bytes < size) {
        const ssize_t n = ::read(fd,
            static_cast<std::uint8_t*>(data) + read_bytes,
            size - read_bytes);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        read_bytes += static_cast<std::size_t>(n);
    }
    return true;
#endif
}

} // namespace

std::uint32_t Journal::readU32BE(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint32_t Journal::crc32(const std::uint8_t* data, std::size_t size) noexcept {
    initializeCRC32();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint8_t index =
            static_cast<std::uint8_t>((crc ^ data[i]) & 0xFFU);
        crc = (crc >> 8U) ^ g_crc_table[index];
    }
    return crc ^ 0xFFFFFFFFU;
}

void Journal::putU8(
    std::array<std::uint8_t, kMaxRecordSize>& out,
    std::size_t& pos, std::uint8_t value) {
    out[pos++] = value;
}

void Journal::putU32BE(
    std::array<std::uint8_t, kMaxRecordSize>& out,
    std::size_t& pos, std::uint32_t value) {
    out[pos++] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    out[pos++] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    out[pos++] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    out[pos++] = static_cast<std::uint8_t>(value & 0xFFU);
}

void Journal::putU64BE(
    std::array<std::uint8_t, kMaxRecordSize>& out,
    std::size_t& pos, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out[pos++] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

void Journal::putI64BE(
    std::array<std::uint8_t, kMaxRecordSize>& out,
    std::size_t& pos, std::int64_t value) {
    putU64BE(out, pos, static_cast<std::uint64_t>(value));
}

Journal::Journal(const std::string& file_path, std::size_t queue_capacity)
    : file_path_(file_path)
    , ring_(queue_capacity)
    , queue_capacity_(queue_capacity) {
    if (queue_capacity_ == 0) {
        throw std::invalid_argument("Journal: queue capacity must be > 0");
    }
    if (queue_capacity_ > JournalRingBuffer::kRingCapacity) {
        queue_capacity_ = JournalRingBuffer::kRingCapacity;
    }

    fd_ = j_open(file_path_.c_str(), JOURNAL_OPEN_FLAGS, JOURNAL_OPEN_MODE);
    if (fd_ < 0) {
        healthy_.store(false, std::memory_order_release);
        throw std::runtime_error("Journal: cannot open file: " + file_path_);
    }

    const auto file_size = j_lseek(fd_, 0, SEEK_END);
    if (file_size < 0) {
        j_close(fd_); fd_ = -1;
        healthy_.store(false, std::memory_order_release);
        throw std::runtime_error("Journal: seek failed");
    }

    if (file_size > 0) {
        if (file_size < 5) {
            j_close(fd_); fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: truncated header");
        }
        if (j_lseek(fd_, 0, SEEK_SET) < 0) {
            j_close(fd_); fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: header seek failed");
        }
        char magic[4]{};
        std::uint8_t version = 0;
        if (!readAll(fd_, magic, 4) || !readAll(fd_, &version, 1)) {
            j_close(fd_); fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: cannot read header");
        }
        if (std::memcmp(magic, kMagic, 4) != 0 || version != kVersion) {
            j_close(fd_); fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: incompatible header");
        }
        if (j_lseek(fd_, 0, SEEK_END) < 0) {
            j_close(fd_); fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: end seek failed");
        }
    } else {
        if (!writeHeader()) {
            j_close(fd_); fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: header write failed");
        }
    }

    owner_thread_id_ = std::this_thread::get_id();
    running_.store(true, std::memory_order_release);
    writer_thread_ = std::thread(&Journal::writerThread, this);
}

Journal::~Journal() {
    {
        std::lock_guard<std::mutex> lock(flush_mutex_);
        running_.store(false, std::memory_order_release);
    }
    notifyQueue();

    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    (void)flush();

    {
        std::lock_guard<std::mutex> file_lock(file_mutex_);
        if (fd_ >= 0) {
            (void)j_sync(fd_);
            (void)j_close(fd_);
            fd_ = -1;
        }
    }
}

void Journal::setSyncPolicy(SyncPolicy policy, std::size_t n) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    sync_policy_ = policy;
    sync_every_n_ = (n == 0) ? 1 : n;
    records_since_sync_ = 0;
}

bool Journal::writeHeader() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (fd_ < 0) return false;
    std::uint8_t header[5]{};
    std::memcpy(header, kMagic, 4);
    header[4] = kVersion;
    return writeAll(fd_, header, sizeof(header));
}

void Journal::notifyQueue() noexcept {
    std::lock_guard<std::mutex> lock(flush_mutex_);
    queue_cv_.notify_one();
}

void Journal::notifyFlush() noexcept {
    std::lock_guard<std::mutex> lock(flush_mutex_);
    flush_cv_.notify_all();
}

void Journal::failStop() noexcept {
    std::lock_guard<std::mutex> lock(flush_mutex_);
    healthy_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    queue_cv_.notify_one();
    flush_cv_.notify_all();
}

bool Journal::enqueueSerialized(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size == 0 || size > kMaxRecordSize) {
        failStop();
        return false;
    }

    if (!healthy_.load(std::memory_order_acquire) ||
        !running_.load(std::memory_order_acquire)) {
        return false;
    }

    const auto result = ring_.tryPush(data, size);

    if (result == JournalRingBuffer::PushResult::Ok) {
        {
            std::lock_guard<std::mutex> lock(flush_mutex_);
            ++queued_records_;
            queue_cv_.notify_one();
        }
        return true;
    }

    failStop();
    return false;
}

bool Journal::validateSerialized(const std::uint8_t* data, std::size_t size) const noexcept {
    if (data == nullptr) return false;
    if (size < kRecordOverhead) return false;
    if (size > kMaxRecordSize) return false;

    const std::uint32_t payload_length = readU32BE(data);
    if (payload_length > kMaxPayloadSize) return false;

    const std::size_t expected =
        kRecordOverhead + static_cast<std::size_t>(payload_length);
    return expected == size;
}

bool Journal::doFdatasync() {
    if (fd_ < 0) return false;
    if (j_sync(fd_) != 0) return false;
    return true;
}

bool Journal::writeSerialized(const std::uint8_t* data, std::size_t size) {
    if (!validateSerialized(data, size)) return false;

    std::lock_guard<std::mutex> lock(file_mutex_);
    if (fd_ < 0) return false;
    if (!writeAll(fd_, data, size)) return false;

    ++records_since_sync_;
    bool should_sync = false;

    if (sync_policy_ == SyncPolicy::PER_RECORD) {
        should_sync = true;
    } else if (sync_policy_ == SyncPolicy::PER_N_RECORDS) {
        if (records_since_sync_ >= sync_every_n_) should_sync = true;
    }

    if (should_sync) {
        if (!doFdatasync()) return false;
        records_since_sync_ = 0;
    }
    return true;
}

void Journal::writerThread() {
    while (true) {
        JournalRingBuffer::RecordView view;

        {
            std::unique_lock<std::mutex> lock(flush_mutex_);
            queue_cv_.wait(lock, [this] {
                return !ring_.empty() ||
                       !running_.load(std::memory_order_acquire);
            });

            if (ring_.empty() &&
                !running_.load(std::memory_order_acquire)) {
                lock.unlock();
                notifyFlush();
                break;
            }
        }

        if (!ring_.tryPop(view)) {
            failStop();
            break;
        }

        if (view.size < kRecordOverhead) {
            failStop();
            break;
        }

        if (!validateSerialized(view.bytes(), view.size)) {
            failStop();
            break;
        }

        std::uint64_t sequence = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            sequence = (sequence << 8U) |
                       static_cast<std::uint64_t>(view.data[4 + i]);
        }

        const bool ok = writeSerialized(view.bytes(), view.size);
        if (!ok) {
            failStop();
            break;
        }

        {
            std::lock_guard<std::mutex> lock(flush_mutex_);
            ++written_records_;
            last_written_sequence_.store(sequence, std::memory_order_release);
        }

        // Fast path (Fix D): only notify if a flush waiter is actually parked.
        if (flush_waiter_present_.load(std::memory_order_acquire)) {
            notifyFlush();
        }
    }
    // Final notify on exit — unconditional (destructor/waiter must be woken).
    notifyFlush();
}

// Fix C: strict return — only true if the durability target was reached.
bool Journal::waitUntilWritten(std::uint64_t target) {
    std::unique_lock<std::mutex> lock(flush_mutex_);
    flush_cv_.wait(lock, [this, target] {
        return written_records_ >= target ||
               !healthy_.load(std::memory_order_acquire) ||
               !running_.load(std::memory_order_acquire);
    });
    // Strict: shutdown/failure must NOT be reported as success.
    return healthy_.load(std::memory_order_acquire)
        && written_records_ >= target;
}

bool Journal::flush() {
    std::uint64_t target = 0;
    {
        std::lock_guard<std::mutex> lock(flush_mutex_);
        target = queued_records_;
        if (written_records_ >= target) {
            return healthy_.load(std::memory_order_acquire);
        }
        if (!healthy_.load(std::memory_order_acquire)) {
            return false;
        }
    }

    // Fix D: mark that a flush waiter is present so the writer knows to notify.
    flush_waiter_present_.store(true, std::memory_order_release);

    const bool result = waitUntilWritten(target);

    flush_waiter_present_.store(false, std::memory_order_release);

    return result;
}

bool Journal::sync() {
    if (!flush()) return false;

    std::lock_guard<std::mutex> lock(file_mutex_);
    if (fd_ < 0) return false;

    if (!doFdatasync()) {
        healthy_.store(false, std::memory_order_release);
        notifyFlush();
        return false;
    }
    records_since_sync_ = 0;
    return true;
}

void Journal::onEvent(const MarketEvent& event) noexcept {
#ifndef NDEBUG
    assert(std::this_thread::get_id() == owner_thread_id_ &&
           "Journal::onEvent called from non-owner thread");
#endif

    try {
        std::array<std::uint8_t, kMaxRecordSize> record{};

        if (std::holds_alternative<OrderAcceptedEvent>(event)) {
            const auto& e = std::get<OrderAcceptedEvent>(event);
            constexpr std::size_t kPayloadSize = 50;

            std::size_t pos = 13;

            putU64BE(record, pos, e.timestamp);
            putU64BE(record, pos, e.orderId);
            putU32BE(record, pos, e.symbolId);
            putU32BE(record, pos, e.traderId);
            putU8(record, pos, e.isBuy ? 1 : 0);
            putI64BE(record, pos, e.priceTicks);
            putU32BE(record, pos, e.quantity);
            putU32BE(record, pos, e.remainingQuantity);
            putU8(record, pos, static_cast<std::uint8_t>(e.orderType));
            putU64BE(record, pos, e.prioritySeq);

            const std::size_t payload_end = pos;
            if (payload_end != 13 + kPayloadSize) {
                failStop();
                return;
            }

            const std::uint32_t payload_length =
                static_cast<std::uint32_t>(kPayloadSize);
            record[0] = static_cast<std::uint8_t>((payload_length >> 24U) & 0xFFU);
            record[1] = static_cast<std::uint8_t>((payload_length >> 16U) & 0xFFU);
            record[2] = static_cast<std::uint8_t>((payload_length >> 8U) & 0xFFU);
            record[3] = static_cast<std::uint8_t>(payload_length & 0xFFU);

            std::size_t seq_pos = 4;
            putU64BE(record, seq_pos, e.sequence);

            record[12] = static_cast<std::uint8_t>(JournalCommand::NEW_ORDER);

            const std::uint32_t checksum = crc32(record.data(), payload_end);
            std::size_t crc_pos = payload_end;
            record[crc_pos++] = static_cast<std::uint8_t>((checksum >> 24U) & 0xFFU);
            record[crc_pos++] = static_cast<std::uint8_t>((checksum >> 16U) & 0xFFU);
            record[crc_pos++] = static_cast<std::uint8_t>((checksum >> 8U) & 0xFFU);
            record[crc_pos++] = static_cast<std::uint8_t>(checksum & 0xFFU);

            if (!enqueueSerialized(record.data(), crc_pos)) return;

        } else if (std::holds_alternative<OrderCancelledEvent>(event)) {
            const auto& e = std::get<OrderCancelledEvent>(event);
            constexpr std::size_t kPayloadSize = 16;

            std::size_t pos = 13;
            putU64BE(record, pos, e.timestamp);
            putU64BE(record, pos, e.orderId);

            if (pos != 13 + kPayloadSize) {
                failStop();
                return;
            }

            const std::uint32_t payload_length =
                static_cast<std::uint32_t>(kPayloadSize);
            record[0] = static_cast<std::uint8_t>((payload_length >> 24U) & 0xFFU);
            record[1] = static_cast<std::uint8_t>((payload_length >> 16U) & 0xFFU);
            record[2] = static_cast<std::uint8_t>((payload_length >> 8U) & 0xFFU);
            record[3] = static_cast<std::uint8_t>(payload_length & 0xFFU);

            std::size_t seq_pos = 4;
            putU64BE(record, seq_pos, e.sequence);

            record[12] = static_cast<std::uint8_t>(JournalCommand::CANCEL_ORDER);

            const std::uint32_t checksum = crc32(record.data(), 13 + kPayloadSize);
            std::size_t crc_pos = 13 + kPayloadSize;
            record[crc_pos++] = static_cast<std::uint8_t>((checksum >> 24U) & 0xFFU);
            record[crc_pos++] = static_cast<std::uint8_t>((checksum >> 16U) & 0xFFU);
            record[crc_pos++] = static_cast<std::uint8_t>((checksum >> 8U) & 0xFFU);
            record[crc_pos++] = static_cast<std::uint8_t>(checksum & 0xFFU);

            if (!enqueueSerialized(record.data(), crc_pos)) return;

        } else if (std::holds_alternative<TradeEvent>(event)) {
            const auto& e = std::get<TradeEvent>(event);
            constexpr std::size_t kPayloadSize = 45;

            std::size_t pos = 13;
            putU64BE(record, pos, e.timestamp);
            putU64BE(record, pos, e.restingOrderId);
            putU64BE(record, pos, e.aggressorOrderId);
            putU32BE(record, pos, e.symbolId);
            putU32BE(record, pos, e.traderId);
            putU32BE(record, pos, e.tradeQuantity);
            putI64BE(record, pos, e.tradePriceTicks);
            putU8(record, pos, e.aggressorIsBuy ? 1 : 0);

            if (pos != 13 + kPayloadSize) {
                failStop();
                return;
            }

            const std::uint32_t payload_length =
                static_cast<std::uint32_t>(kPayloadSize);
            record[0] = static_cast<std::uint8_t>((payload_length >> 24U) & 0xFFU);
            record[1] = static_cast<std::uint8_t>((payload_length >> 16U) & 0xFFU);
            record[2] = static_cast<std::uint8_t>((payload_length >> 8U) & 0xFFU);
            record[3] = static_cast<std::uint8_t>(payload_length & 0xFFU);

            std::size_t seq_pos = 4;
            putU64BE(record, seq_pos, e.sequence);

            record[12] = static_cast<std::uint8_t>(JournalCommand::FILL);

            const std::uint32_t checksum = crc32(record.data(), 13 + kPayloadSize);
            std::size_t crc_pos = 13 + kPayloadSize;
            record[crc_pos++] = static_cast<std::uint8_t>((checksum >> 24U) & 0xFFU);
            record[crc_pos++] = static_cast<std::uint8_t>((checksum >> 16U) & 0xFFU);
            record[crc_pos++] = static_cast<std::uint8_t>((checksum >> 8U) & 0xFFU);
            record[crc_pos++] = static_cast<std::uint8_t>(checksum & 0xFFU);

            if (!enqueueSerialized(record.data(), crc_pos)) return;
        }
        // OrderRejectedEvent is intentionally not journaled.
    }
    catch (...) {
        failStop();
    }
}