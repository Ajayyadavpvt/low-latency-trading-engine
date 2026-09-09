#include "Recovery.h"
#include "Journal.h"
#include <array>
#include <fstream>

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

std::uint32_t Recovery::crc32(const std::uint8_t* data, std::size_t size) noexcept {
    initializeCRC32();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        std::uint8_t index = static_cast<std::uint8_t>((crc ^ data[i]) & 0xFFU);
        crc = (crc >> 8U) ^ crc_table[index];
    }
    return crc ^ 0xFFFFFFFFU;
}

std::uint32_t Recovery::readU32BE(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint64_t Recovery::readU64BE(const std::uint8_t* p) {
    std::uint64_t r = 0;
    for (int i = 0; i < 8; ++i) r = (r << 8) | p[i];
    return r;
}

std::int64_t Recovery::readI64BE(const std::uint8_t* p) {
    return static_cast<std::int64_t>(readU64BE(p));
}

Recovery::Recovery(const std::string& file_path) : file_path_(file_path) {}

bool Recovery::readHeader(std::ifstream& file) {
    std::array<char, 4> magic;
    file.read(magic.data(), 4);
    if (!file) return false;
    if (magic[0] != 'J' || magic[1] != 'N' || magic[2] != 'L' || magic[3] != '2') return false;
    std::uint8_t version;
    file.read(reinterpret_cast<char*>(&version), 1);
    if (!file) return false;
    return version == kVersion;
}

bool Recovery::validateSequence(std::uint64_t sequence) {
    if (first_record_) { first_record_ = false; last_sequence_ = sequence; return true; }
    if (sequence <= last_sequence_) return false;
    last_sequence_ = sequence;
    return true;
}

bool Recovery::readRecord(std::ifstream& file, Record& record, bool& clean_eof, bool& truncated) {
    clean_eof = false; truncated = false;
    std::array<std::uint8_t, 4> len_bytes;
    file.read(reinterpret_cast<char*>(len_bytes.data()), 4);
    if (file.eof()) { clean_eof = true; return false; }
    if (!file) { truncated = true; return false; }
    std::uint32_t payload_length = readU32BE(len_bytes.data());
    constexpr std::uint32_t kFixedBody = 4 + 8 + 1;
    if (payload_length > 1024 * 1024) return false;

    std::vector<std::uint8_t> body;
    body.resize(kFixedBody + payload_length);
    body[0] = len_bytes[0]; body[1] = len_bytes[1]; body[2] = len_bytes[2]; body[3] = len_bytes[3];
    file.read(reinterpret_cast<char*>(body.data() + 4), body.size() - 4);
    if (!file) { truncated = true; return false; }

    std::array<std::uint8_t, 4> crc_bytes;
    file.read(reinterpret_cast<char*>(crc_bytes.data()), 4);
    if (!file) { truncated = true; return false; }
    std::uint32_t stored_crc = readU32BE(crc_bytes.data());
    std::uint32_t calculated_crc = crc32(body.data(), body.size());
    if (stored_crc != calculated_crc) return false;

    record.sequence = readU64BE(body.data() + 4);
    record.command = body[12];
    record.payload.assign(body.begin() + 13, body.end());
    return true;
}

bool Recovery::processRecord(MatchingEngine& engine, const Record& record) {
    if (record.command == static_cast<std::uint8_t>(JournalCommand::NEW_ORDER)) {
        if (record.payload.size() != 42) return false;
        const auto* p = record.payload.data();
        std::uint64_t timestamp = readU64BE(p);
        std::uint64_t order_id = readU64BE(p+8);
        std::uint32_t symbol_id = readU32BE(p+16);
        std::uint32_t trader_id = readU32BE(p+20);
        std::uint8_t side = p[24];
        std::int64_t price_ticks = readI64BE(p+25);
        std::uint32_t quantity = readU32BE(p+33);
        std::uint32_t remaining = readU32BE(p+37);
        std::uint8_t order_type = p[41];
        if (side > 1 || quantity == 0 || remaining == 0 || remaining > quantity) return false;
        if (order_type > static_cast<std::uint8_t>(OrderType::FOK)) return false;
        OrderSide os = side == 1 ? OrderSide::BUY : OrderSide::SELL;
        OrderType ot = static_cast<OrderType>(order_type);
        double price = static_cast<double>(price_ticks) / 1000000.0;
        Order order(order_id, trader_id, os, ot, price, quantity, symbol_id);
        order.setTimestamp(std::chrono::nanoseconds(timestamp));
        if (!engine.restoreOrder(order, remaining)) return false;
        ++records_replayed_;
        return true;
    } else if (record.command == static_cast<std::uint8_t>(JournalCommand::CANCEL_ORDER)) {
        if (record.payload.size() != 16) return false;
        std::uint64_t order_id = readU64BE(record.payload.data() + 8);
        if (!engine.cancelOrder(order_id)) return false;
        ++records_replayed_;
        return true;
    } else if (record.command == static_cast<std::uint8_t>(JournalCommand::FILL)) {
        if (record.payload.size() != 40) return false;
        const auto* p = record.payload.data();
        std::uint64_t resting_id = readU64BE(p+8);
        std::uint32_t fill_qty = readU32BE(p+24);
        if (!engine.applyFill(resting_id, fill_qty)) return false;
        ++records_replayed_;
        return true;
    }
    return false;
}

bool Recovery::replay(MatchingEngine& engine) {
    records_replayed_ = 0; last_sequence_ = 0; first_record_ = true; truncated_tail_ = false;
    std::ifstream file(file_path_, std::ios::binary);
    if (!file) return false;
    if (!readHeader(file)) return false;
    while (true) {
        Record record;
        bool clean_eof = false, truncated = false;
        if (!readRecord(file, record, clean_eof, truncated)) {
            if (clean_eof) return true;
            if (truncated) { truncated_tail_ = true; return true; }
            return false;
        }
        if (!validateSequence(record.sequence)) return false;
        if (!processRecord(engine, record)) return false;
    }
}