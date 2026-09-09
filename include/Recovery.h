#pragma once
#include "MatchingEngine.h"
#include <cstdint>
#include <string>
#include <vector>

class Recovery {
public:
    explicit Recovery(const std::string& file_path);
    Recovery(const Recovery&) = delete;
    Recovery& operator=(const Recovery&) = delete;

    bool replay(MatchingEngine& engine);

    std::uint64_t recordsReplayed() const noexcept { return records_replayed_; }
    std::uint64_t lastSequence() const noexcept { return last_sequence_; }
    bool hadTruncatedTail() const noexcept { return truncated_tail_; }

private:
    static constexpr std::uint8_t kVersion = 2;

    struct Record {
        std::uint64_t sequence;
        std::uint8_t command;
        std::vector<std::uint8_t> payload;
    };

    bool readHeader(std::ifstream& file);
    bool readRecord(std::ifstream& file, Record& record, bool& clean_eof, bool& truncated);
    bool processRecord(MatchingEngine& engine, const Record& record);
    bool validateSequence(std::uint64_t sequence);

    static std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept;
    static std::uint32_t readU32BE(const std::uint8_t* p);
    static std::uint64_t readU64BE(const std::uint8_t* p);
    static std::int64_t readI64BE(const std::uint8_t* p);

    std::string file_path_;
    std::uint64_t records_replayed_{0};
    std::uint64_t last_sequence_{0};
    bool first_record_{true};
    bool truncated_tail_{false};
};