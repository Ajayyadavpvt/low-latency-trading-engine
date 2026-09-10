#include "Journal.h"
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
    #include <io.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #define JOURNAL_OPEN_FLAGS (O_RDWR | O_CREAT | O_BINARY)
    #define JOURNAL_OPEN_MODE  (_S_IREAD | _S_IWRITE)

    static int j_open(const char* path, int flags, int mode) {
        return _open(path, flags, mode);
    }
    static int j_close(int fd) { return _close(fd); }
    static long long j_lseek(int fd, long long off, int whence) {
        return _lseeki64(fd, off, whence);
    }
    static int j_read(int fd, void* buf, std::size_t count) {
        return _read(fd, buf, static_cast<unsigned int>(count));
    }
    static int j_write(int fd, const void* buf, std::size_t count) {
        return _write(fd, buf, static_cast<unsigned int>(count));
    }
    static int j_sync(int fd) { return _commit(fd); }
#else
    #include <fcntl.h>
    #include <unistd.h>
    #define JOURNAL_OPEN_FLAGS (O_RDWR | O_CREAT)
    #define JOURNAL_OPEN_MODE  (0644)

    static int j_open(const char* path, int flags, int mode) {
        return ::open(path, flags, mode);
    }
    static int j_close(int fd) { return ::close(fd); }
    static long long j_lseek(int fd, long long off, int whence) {
        return static_cast<long long>(::lseek(fd, off, whence));
    }
    static int j_read(int fd, void* buf, std::size_t count) {
        return static_cast<int>(::read(fd, buf, count));
    }
    static int j_write(int fd, const void* buf, std::size_t count) {
        return static_cast<int>(::write(fd, buf, count));
    }
    static int j_sync(int fd) { return ::fdatasync(fd); }
#endif

namespace {

// Cross-platform helper: write all bytes or fail
bool writeAll(int fd, const void* data, std::size_t size) {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    std::size_t remaining = size;
    while (remaining > 0) {
        int n = j_write(fd, p, remaining);
        if (n <= 0) return false;
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

// Cross-platform helper: read all bytes or fail
bool readAll(int fd, void* data, std::size_t size) {
    std::uint8_t* p = static_cast<std::uint8_t*>(data);
    std::size_t remaining = size;
    while (remaining > 0) {
        int n = j_read(fd, p, remaining);
        if (n <= 0) return false;
        p += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

std::uint32_t crc_table[256];
std::once_flag crc_once;
void initializeCRC32() {
    std::call_once(crc_once, [] {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                if (crc & 1U) crc = (crc >> 1U) ^ 0xEDB88320U;
                else crc >>= 1U;
            }
            crc_table[i] = crc;
        }
    });
}

} // namespace

std::uint32_t Journal::crc32(const std::uint8_t* data, std::size_t size) noexcept {
    initializeCRC32();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        std::uint8_t index = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFFU);
        crc = (crc >> 8U) ^ crc_table[index];
    }
    return crc ^ 0xFFFFFFFFU;
}

void Journal::appendU8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }
void Journal::appendU32BE(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back((v >> 24) & 0xFF); out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 8) & 0xFF); out.push_back(v & 0xFF);
}
void Journal::appendU64BE(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) out.push_back((v >> shift) & 0xFF);
}
void Journal::appendI64BE(std::vector<std::uint8_t>& out, std::int64_t v) {
    appendU64BE(out, static_cast<std::uint64_t>(v));
}

Journal::Journal(const std::string& file_path, std::size_t queue_capacity)
    : file_path_(file_path), queue_capacity_(queue_capacity) {
    if (queue_capacity_ == 0) {
        throw std::invalid_argument("Journal: queue capacity must be > 0");
    }

    // Open with O_RDWR so we can both validate header (read) and append (write)
    fd_ = j_open(file_path_.c_str(), JOURNAL_OPEN_FLAGS, JOURNAL_OPEN_MODE);
    if (fd_ < 0) {
        healthy_.store(false, std::memory_order_release);
        throw std::runtime_error("Journal: cannot open file: " + file_path_);
    }

    // Determine current file size
    long long file_size = j_lseek(fd_, 0, SEEK_END);
    if (file_size < 0) {
        j_close(fd_);
        fd_ = -1;
        healthy_.store(false, std::memory_order_release);
        throw std::runtime_error("Journal: seek end failed");
    }

    // Validate existing header if file non-empty
    if (file_size > 0) {
        if (file_size < 5) {
            j_close(fd_);
            fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: truncated header");
        }
        if (j_lseek(fd_, 0, SEEK_SET) < 0) {
            j_close(fd_);
            fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: seek start failed");
        }
        char magic[4];
        std::uint8_t version = 0;
        if (!readAll(fd_, magic, 4) || !readAll(fd_, &version, 1)) {
            j_close(fd_);
            fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: cannot read header");
        }
        if (std::memcmp(magic, kMagic, 4) != 0 || version != kVersion) {
            j_close(fd_);
            fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: incompatible or corrupt header");
        }
    }

    // Move to end for appending
    if (j_lseek(fd_, 0, SEEK_END) < 0) {
        j_close(fd_);
        fd_ = -1;
        healthy_.store(false, std::memory_order_release);
        throw std::runtime_error("Journal: seek end failed");
    }

    // If file was empty, write header
    if (file_size == 0) {
        if (!writeHeader()) {
            j_close(fd_);
            fd_ = -1;
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: failed to write header");
        }
    }

    running_.store(true, std::memory_order_release);
    writer_thread_ = std::thread(&Journal::writerThread, this);
}

Journal::~Journal() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        running_.store(false, std::memory_order_release);
    }
    queue_cv_.notify_all();
    if (writer_thread_.joinable()) writer_thread_.join();

    // Final flush and sync
    flush();

    std::lock_guard<std::mutex> lock(file_mutex_);
    if (fd_ >= 0) {
        j_sync(fd_);
        j_close(fd_);
        fd_ = -1;
    }
}

bool Journal::writeHeader() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (fd_ < 0) return false;

    std::uint8_t buf[5];
    std::memcpy(buf, kMagic, 4);
    buf[4] = kVersion;
    return writeAll(fd_, buf, sizeof(buf));
}

bool Journal::enqueue(JournalRecord&& record) noexcept {
    try {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!running_.load(std::memory_order_acquire)) {
            healthy_.store(false, std::memory_order_release);
            return false;
        }
        if (queue_.size() >= queue_capacity_) {
            healthy_.store(false, std::memory_order_release);
            return false;
        }
        queue_.emplace_back(std::move(record));
        ++queued_records_;
    } catch (...) {
        healthy_.store(false, std::memory_order_release);
        return false;
    }
    queue_cv_.notify_one();
    return true;
}

bool Journal::writeRecord(const JournalRecord& record) {
    std::vector<std::uint8_t> body;
    body.reserve(13 + record.payload.size());
    appendU32BE(body, static_cast<std::uint32_t>(record.payload.size()));
    appendU64BE(body, record.sequence);
    appendU8(body, static_cast<std::uint8_t>(record.command));
    body.insert(body.end(), record.payload.begin(), record.payload.end());

    std::uint32_t checksum = crc32(body.data(), body.size());
    std::vector<std::uint8_t> crc_bytes;
    crc_bytes.reserve(4);
    appendU32BE(crc_bytes, checksum);

    std::lock_guard<std::mutex> lock(file_mutex_);
    if (fd_ < 0) return false;
    if (!writeAll(fd_, body.data(), body.size())) return false;
    if (!writeAll(fd_, crc_bytes.data(), crc_bytes.size())) return false;
    return true;
}

void Journal::writerThread() {
    while (true) {
        JournalRecord record;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !queue_.empty() || !running_.load(std::memory_order_acquire);
            });

            if (queue_.empty() && !running_.load(std::memory_order_acquire)) {
                break;
            }

            record = std::move(queue_.front());
            queue_.pop_front();
        }

        const bool ok = writeRecord(record);

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (ok) {
                ++written_records_;
                last_written_sequence_.store(record.sequence, std::memory_order_release);
            } else {
                healthy_.store(false, std::memory_order_release);
                running_.store(false, std::memory_order_release);
            }
            // FIX: Notify after EVERY write, not just when queue fully drained.
            // This prevents flush() from blocking indefinitely under continuous load.
            drained_cv_.notify_all();
        }

        if (!ok) {
            queue_cv_.notify_all();
            break;
        }
    }
}

bool Journal::waitUntilWritten(std::uint64_t target) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    drained_cv_.wait(lock, [this, target] {
        return written_records_ >= target ||
               !healthy_.load(std::memory_order_acquire);
    });
    return healthy_.load(std::memory_order_acquire);
}

bool Journal::flush() {
    // Snapshot target: number of records enqueued up to this instant
    std::uint64_t target;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        target = queued_records_;
    }

    // Wait until writer has processed all records up to that target
    if (!waitUntilWritten(target)) {
        return false;
    }

    // Data is written directly to fd (no user-space buffering to flush)
    return healthy_.load(std::memory_order_acquire);
}

bool Journal::sync() {
    if (!flush()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(file_mutex_);
    if (fd_ < 0) return false;

    if (j_sync(fd_) != 0) {
        healthy_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void Journal::onEvent(const MarketEvent& event) noexcept {
    try {
        if (std::holds_alternative<OrderAcceptedEvent>(event)) {
            const auto& e = std::get<OrderAcceptedEvent>(event);
            std::vector<std::uint8_t> payload;
            payload.reserve(50);
            appendU64BE(payload, e.timestamp);
            appendU64BE(payload, e.orderId);
            appendU32BE(payload, e.symbolId);
            appendU32BE(payload, e.traderId);
            appendU8(payload, e.isBuy ? 1 : 0);
            appendI64BE(payload, e.priceTicks);
            appendU32BE(payload, e.quantity);
            appendU32BE(payload, e.remainingQuantity);
            appendU8(payload, static_cast<std::uint8_t>(e.orderType));
            JournalRecord record(e.sequence, JournalCommand::NEW_ORDER, std::move(payload));
            enqueue(std::move(record));
        } else if (std::holds_alternative<OrderCancelledEvent>(event)) {
            const auto& e = std::get<OrderCancelledEvent>(event);
            std::vector<std::uint8_t> payload;
            payload.reserve(16);
            appendU64BE(payload, e.timestamp);
            appendU64BE(payload, e.orderId);
            JournalRecord record(e.sequence, JournalCommand::CANCEL_ORDER, std::move(payload));
            enqueue(std::move(record));
        } else if (std::holds_alternative<TradeEvent>(event)) {
            const auto& e = std::get<TradeEvent>(event);
            std::vector<std::uint8_t> payload;
            payload.reserve(40);
            appendU64BE(payload, e.timestamp);
            appendU64BE(payload, e.restingOrderId);
            appendU64BE(payload, e.aggressorOrderId);
            appendU32BE(payload, e.symbolId);
            appendU32BE(payload, e.tradeQuantity);
            appendI64BE(payload, e.tradePriceTicks);
            JournalRecord record(e.sequence, JournalCommand::FILL, std::move(payload));
            enqueue(std::move(record));
        }
        // OrderRejectedEvent ignored
    } catch (...) {
        healthy_.store(false, std::memory_order_release);
    }
}