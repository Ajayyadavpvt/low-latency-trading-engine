#pragma once

#include <cstdint>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include "MatchingEngine.h"

class Recovery {
public:
    explicit Recovery(const std::string& file_path);

    bool replay(MatchingEngine& engine);

    // Returns false if max priority was UINT64_MAX (overflow)
    bool getNextPrioritySequence(
        std::uint64_t& out) const noexcept;

    // Compatibility helper (returns UINT64_MAX on overflow)
    std::uint64_t nextPrioritySequence() const noexcept;

    std::uint64_t lastPrioritySeqSeen() const noexcept {
        return max_priority_seq_seen_;
    }

    std::uint64_t recordsReplayed() const noexcept {
        return records_replayed_;
    }

    std::uint64_t lastSequence() const noexcept {
        return last_sequence_;
    }

    bool hadTruncatedTail() const noexcept {
        return truncated_tail_;
    }

private:
    static constexpr std::uint8_t kVersion = 3;

    struct Record {
        std::uint64_t sequence = 0;
        std::uint8_t command = 0;
        std::vector<std::uint8_t> payload;
    };

    std::string file_path_;

    std::uint64_t records_replayed_{0};
    std::uint64_t new_orders_replayed_{0};   // for priority tracking only
    std::uint64_t last_sequence_{0};

    bool first_record_{true};
    bool truncated_tail_{false};

    std::uint64_t max_priority_seq_seen_{0};

    bool readHeader(std::ifstream& file);

    bool validateSequence(std::uint64_t sequence);

    bool readRecord(
        std::ifstream& file,
        Record& record,
        bool& clean_eof,
        bool& truncated);

    bool processRecord(
        MatchingEngine& engine,
        const Record& record);

    bool validatePrioritySequence(std::uint64_t priority_seq);

    static std::uint32_t crc32(
        const std::uint8_t* data,
        std::size_t size) noexcept;

    static std::uint32_t readU32BE(const std::uint8_t* p);
    static std::uint64_t readU64BE(const std::uint8_t* p);
    static std::int64_t readI64BE(const std::uint8_t* p);
};