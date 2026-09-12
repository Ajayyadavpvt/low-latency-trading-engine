#include "Recovery.h"
#include "Journal.h"

#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <mutex>

namespace {

std::uint32_t crc_table[256];
std::once_flag crc_once;

void initializeCRC32() {
    std::call_once(crc_once, [] {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                if (crc & 1U) {
                    crc = (crc >> 1U) ^ 0xEDB88320U;
                } else {
                    crc >>= 1U;
                }
            }
            crc_table[i] = crc;
        }
    });
}

} // namespace

std::uint32_t Recovery::crc32(
    const std::uint8_t* data,
    std::size_t size) noexcept
{
    initializeCRC32();

    std::uint32_t crc = 0xFFFFFFFFU;

    for (std::size_t i = 0; i < size; ++i) {
        std::uint8_t index =
            static_cast<std::uint8_t>((crc ^ data[i]) & 0xFFU);

        crc = (crc >> 8U) ^ crc_table[index];
    }

    return crc ^ 0xFFFFFFFFU;
}

std::uint32_t Recovery::readU32BE(const std::uint8_t* p) {
    return
        (static_cast<std::uint32_t>(p[0]) << 24) |
        (static_cast<std::uint32_t>(p[1]) << 16) |
        (static_cast<std::uint32_t>(p[2]) << 8) |
        static_cast<std::uint32_t>(p[3]);
}

std::uint64_t Recovery::readU64BE(const std::uint8_t* p) {
    std::uint64_t r = 0;
    for (int i = 0; i < 8; ++i) {
        r = (r << 8) | p[i];
    }
    return r;
}

std::int64_t Recovery::readI64BE(const std::uint8_t* p) {
    return static_cast<std::int64_t>(readU64BE(p));
}

Recovery::Recovery(const std::string& file_path)
    : file_path_(file_path)
{
}

bool Recovery::readHeader(std::ifstream& file) {
    std::array<char, 4> magic{};

    file.read(
        magic.data(),
        static_cast<std::streamsize>(magic.size()));

    if (!file) {
        return false;
    }

    if (magic[0] != 'J' ||
        magic[1] != 'N' ||
        magic[2] != 'L' ||
        magic[3] != '2') {
        return false;
    }

    std::uint8_t version = 0;

    file.read(
        reinterpret_cast<char*>(&version),
        1);

    if (!file) {
        return false;
    }

    // Only v3 supported; v2 or unknown versions explicitly rejected.
    if (version != kVersion) {
        return false;
    }

    return true;
}

bool Recovery::validateSequence(std::uint64_t sequence) {
    if (first_record_) {
        first_record_ = false;
        last_sequence_ = sequence;
        return true;
    }

    // Prevent wrap-around from making MAX -> 0 look valid.
    if (last_sequence_ == UINT64_MAX) {
        return false;
    }

    if (sequence != last_sequence_ + 1) {
        return false;
    }

    last_sequence_ = sequence;
    return true;
}

bool Recovery::validatePrioritySequence(std::uint64_t priority_seq) {
    if (new_orders_replayed_ == 0) {
        max_priority_seq_seen_ = priority_seq;
        return true;
    }

    // Priority must be strictly increasing. Gaps are allowed because
    // IOC/MARKET/FOK orders consume priority but are not journaled.
    if (priority_seq <= max_priority_seq_seen_) {
        return false;
    }

    max_priority_seq_seen_ = priority_seq;
    return true;
}

bool Recovery::getNextPrioritySequence(
    std::uint64_t& out) const noexcept
{
    if (new_orders_replayed_ == 0) {
        out = 0;
        return true;
    }

    if (max_priority_seq_seen_ == UINT64_MAX) {
        return false;
    }

    out = max_priority_seq_seen_ + 1;
    return true;
}

std::uint64_t Recovery::nextPrioritySequence() const noexcept {
    std::uint64_t out = 0;
    if (!getNextPrioritySequence(out)) {
        return UINT64_MAX;
    }
    return out;
}

bool Recovery::readRecord(
    std::ifstream& file,
    Record& record,
    bool& clean_eof,
    bool& truncated)
{
    clean_eof = false;
    truncated = false;

    std::array<std::uint8_t, 4> len_bytes{};

    file.read(
        reinterpret_cast<char*>(len_bytes.data()),
        4);

    if (file.eof()) {
        clean_eof = true;
        return false;
    }

    if (!file) {
        truncated = true;
        return false;
    }

    const std::uint32_t payload_length =
        readU32BE(len_bytes.data());

    constexpr std::uint32_t kFixedBody = 4 + 8 + 1;

    if (payload_length > Journal::kMaxPayloadSize) {
        return false;
    }

    std::vector<std::uint8_t> body;
    body.resize(kFixedBody + payload_length);

    body[0] = len_bytes[0];
    body[1] = len_bytes[1];
    body[2] = len_bytes[2];
    body[3] = len_bytes[3];

    file.read(
        reinterpret_cast<char*>(body.data() + 4),
        static_cast<std::streamsize>(body.size() - 4));

    if (!file) {
        truncated = true;
        return false;
    }

    std::array<std::uint8_t, 4> crc_bytes{};

    file.read(
        reinterpret_cast<char*>(crc_bytes.data()),
        4);

    if (!file) {
        truncated = true;
        return false;
    }

    const std::uint32_t stored_crc =
        readU32BE(crc_bytes.data());

    const std::uint32_t calculated_crc =
        crc32(body.data(), body.size());

    if (stored_crc != calculated_crc) {
        return false;
    }

    record.sequence = readU64BE(body.data() + 4);
    record.command = body[12];
    record.payload.assign(body.begin() + 13, body.end());

    return true;
}

bool Recovery::processRecord(
    MatchingEngine& engine,
    const Record& record)
{
    if (record.command ==
        static_cast<std::uint8_t>(JournalCommand::NEW_ORDER)) {

        if (record.payload.size() != 50) {
            return false;
        }

        const auto* p = record.payload.data();

        const std::uint64_t timestamp = readU64BE(p);
        const std::uint64_t order_id = readU64BE(p + 8);
        const std::uint32_t symbol_id = readU32BE(p + 16);
        const std::uint32_t trader_id = readU32BE(p + 20);
        const std::uint8_t side = p[24];
        const std::int64_t price_ticks = readI64BE(p + 25);
        const std::uint32_t quantity = readU32BE(p + 33);
        const std::uint32_t remaining = readU32BE(p + 37);
        const std::uint8_t order_type = p[41];
        const std::uint64_t priority_seq = readU64BE(p + 42);

        if (side > 1 ||
            quantity == 0 ||
            remaining == 0 ||
            remaining > quantity) {
            return false;
        }

        if (order_type > static_cast<std::uint8_t>(OrderType::FOK)) {
            return false;
        }

        // Only LIMIT orders should ever be journaled as resting.
        if (order_type != static_cast<std::uint8_t>(OrderType::LIMIT)) {
            return false;
        }

        if (!validatePrioritySequence(priority_seq)) {
            return false;
        }

        const OrderSide os =
            side == 1 ? OrderSide::BUY : OrderSide::SELL;

        const OrderType ot = static_cast<OrderType>(order_type);

        const double price =
            static_cast<double>(price_ticks) / 1000000.0;

        Order order(
            order_id,
            trader_id,
            os,
            ot,
            price,
            quantity,
            symbol_id);

        order.setTimestamp(std::chrono::nanoseconds(timestamp));
        order.priority_seq = priority_seq;

        if (!engine.restoreOrder(order, remaining)) {
            return false;
        }

        ++new_orders_replayed_;
        ++records_replayed_;
        return true;
    }

    if (record.command ==
        static_cast<std::uint8_t>(JournalCommand::CANCEL_ORDER)) {

        if (record.payload.size() != 16) {
            return false;
        }

        const std::uint64_t order_id =
            readU64BE(record.payload.data() + 8);

        if (!engine.restoreCancel(order_id)) {
            return false;
        }

        ++records_replayed_;
        return true;
    }

    if (record.command ==
        static_cast<std::uint8_t>(JournalCommand::FILL)) {

        if (record.payload.size() != 45) {
            return false;
        }

        const auto* p = record.payload.data();
        const std::uint64_t resting_id = readU64BE(p + 8);
        const std::uint32_t fill_qty = readU32BE(p + 32);

        if (!engine.applyFill(resting_id, fill_qty)) {
            return false;
        }

        ++records_replayed_;
        return true;
    }

    return false;
}

bool Recovery::replay(MatchingEngine& engine) {
    records_replayed_ = 0;
    new_orders_replayed_ = 0;
    last_sequence_ = 0;
    first_record_ = true;
    truncated_tail_ = false;
    max_priority_seq_seen_ = 0;

    std::ifstream file(file_path_, std::ios::binary);

    if (!file) {
        engine.halt();
        return false;
    }

    engine.beginRecovery();

    if (!readHeader(file)) {
        engine.halt();
        return false;
    }

    while (true) {
        Record record;

        bool clean_eof = false;
        bool truncated = false;

        if (!readRecord(file, record, clean_eof, truncated)) {
            if (clean_eof) {
                break;
            }

            if (truncated) {
                truncated_tail_ = true;
                break;
            }

            engine.halt();
            return false;
        }

        if (!validateSequence(record.sequence)) {
            engine.halt();
            return false;
        }

        if (!processRecord(engine, record)) {
            engine.halt();
            return false;
        }
    }

    // Auto-seed next priority after successful replay.
    std::uint64_t next_priority = 0;

    if (!getNextPrioritySequence(next_priority)) {
        engine.halt();
        return false;
    }

    if (!engine.seedPrioritySequence(next_priority)) {
        engine.halt();
        return false;
    }

    if (!engine.markReady()) {
        engine.halt();
        return false;
    }

    return true;
}