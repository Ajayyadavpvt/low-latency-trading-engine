#include "Journal.h"
#include <chrono>
#include <cstring>
#include <stdexcept>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <ext/stdio_filebuf.h>
#endif

namespace {
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
}

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
    if (queue_capacity_ == 0) throw std::invalid_argument("queue capacity must be > 0");
    file_.open(file_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
    if (!file_) {
        healthy_.store(false, std::memory_order_release);
        throw std::runtime_error("Journal: cannot open file: " + file_path_);
    }

    // Header validation for existing file
    file_.seekg(0, std::ios::end);
    std::streamoff file_size = file_.tellg();
    if (file_size > 0) {
        file_.seekg(0, std::ios::beg);
        char magic[4];
        std::uint8_t version;
        file_.read(magic, 4);
        file_.read(reinterpret_cast<char*>(&version), 1);
        if (std::memcmp(magic, kMagic, 4) != 0 || version != kVersion) {
            file_.close();
            healthy_.store(false, std::memory_order_release);
            throw std::runtime_error("Journal: incompatible or corrupt header");
        }
        file_.clear();
    }

    file_.seekp(0, std::ios::end);
    if (file_size == 0) {
        if (!writeHeader()) {
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
    flush();
    std::lock_guard<std::mutex> file_lock(file_mutex_);
    if (file_.is_open()) { file_.flush(); file_.close(); }
}

bool Journal::writeHeader() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (!file_) return false;
    file_.write(kMagic, 4);
    std::uint8_t version = kVersion;
    file_.write(reinterpret_cast<const char*>(&version), 1);
    file_.flush();
    return static_cast<bool>(file_);
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
    if (!file_) return false;
    file_.write(reinterpret_cast<const char*>(body.data()), body.size());
    file_.write(reinterpret_cast<const char*>(crc_bytes.data()), crc_bytes.size());
    return static_cast<bool>(file_);
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

            ++written_records_;

            if (ok) {
                last_written_sequence_.store(record.sequence, std::memory_order_release);
            } else {
                healthy_.store(false, std::memory_order_release);
                running_.store(false, std::memory_order_release);
            }

            if (queue_.empty() && written_records_ >= queued_records_) {
                drained_cv_.notify_all();
            }
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

bool Journal::waitUntilDrained() {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    drained_cv_.wait(lock, [this] {
        return queue_.empty() && written_records_ >= queued_records_;
    });

    return healthy_.load(std::memory_order_acquire);
}

bool Journal::flush() {
    std::uint64_t target;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        target = queued_records_;
    }

    if (!waitUntilWritten(target)) {
        return false;
    }

    std::lock_guard<std::mutex> file_lock(file_mutex_);
    if (!file_) return false;

    file_.flush();
    if (!file_) {
        healthy_.store(false, std::memory_order_release);
        return false;
    }

    return true;
}

bool Journal::sync() {
    if (!flush()) {
        return false;
    }

#ifdef __linux__
    std::lock_guard<std::mutex> file_lock(file_mutex_);
    auto* fb = static_cast<__gnu_cxx::stdio_filebuf<char>*>(file_.rdbuf());
    int fd = fb ? fb->fd() : -1;
    if (fd >= 0) {
        if (::fdatasync(fd) != 0) {
            healthy_.store(false, std::memory_order_release);
            return false;
        }
        return true;
    } else {
        return false;
    }
#else
    // On non-Linux, fallback to flush (no durability guarantee)
    return true;
#endif
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