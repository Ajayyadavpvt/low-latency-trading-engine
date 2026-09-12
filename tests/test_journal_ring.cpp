#include <gtest/gtest.h>

#include "JournalRingBuffer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace {

std::array<std::uint8_t, 64>
makeRecord(std::uint8_t value)
{
    std::array<std::uint8_t, 64> data{};

    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] =
            static_cast<std::uint8_t>(
                value + i);
    }

    return data;
}

} // namespace

TEST(JournalRing, EmptyInitially)
{
    JournalRingBuffer ring;

    EXPECT_TRUE(ring.empty());
    EXPECT_FALSE(ring.full());
    EXPECT_EQ(ring.approximateSize(), 0u);
}

TEST(JournalRing, InvalidNullPointer)
{
    JournalRingBuffer ring;

    const auto result =
        ring.tryPush(nullptr, 10);

    EXPECT_EQ(
        result,
        JournalRingBuffer::PushResult::Invalid);
}

TEST(JournalRing, InvalidZeroLength)
{
    JournalRingBuffer ring;

    std::uint8_t value = 0xAA;

    const auto result =
        ring.tryPush(&value, 0);

    EXPECT_EQ(
        result,
        JournalRingBuffer::PushResult::Invalid);
}

TEST(JournalRing, InvalidOversizedRecord)
{
    JournalRingBuffer ring;

    std::vector<std::uint8_t> data(
        JournalRingBuffer::kMaxRecordSize + 1,
        0xAB);

    const auto result =
        ring.tryPush(data.data(), data.size());

    EXPECT_EQ(
        result,
        JournalRingBuffer::PushResult::Invalid);
}

TEST(JournalRing, MaximumRecordSizeAccepted)
{
    JournalRingBuffer ring;

    std::array<std::uint8_t,
               JournalRingBuffer::kMaxRecordSize>
        data{};

    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(i & 0xFFU);
    }

    EXPECT_EQ(
        ring.tryPush(data.data(), data.size()),
        JournalRingBuffer::PushResult::Ok);

    JournalRingBuffer::RecordView view;

    ASSERT_TRUE(ring.tryPop(view));

    ASSERT_EQ(view.size, data.size());

    EXPECT_EQ(
        std::memcmp(view.bytes(), data.data(), data.size()),
        0);

    EXPECT_TRUE(ring.empty());
}

TEST(JournalRing, StrictFIFO)
{
    JournalRingBuffer ring;

    constexpr std::size_t kRecords = 10000;

    for (std::size_t i = 0; i < kRecords; ++i) {
        auto data = makeRecord(
            static_cast<std::uint8_t>(i & 0xFFU));

        ASSERT_EQ(
            ring.tryPush(data.data(), data.size(), i, i, 1),
            JournalRingBuffer::PushResult::Ok);
    }

    for (std::size_t i = 0; i < kRecords; ++i) {
        JournalRingBuffer::RecordView view;

        ASSERT_TRUE(ring.tryPop(view));

        const auto expected = makeRecord(
            static_cast<std::uint8_t>(i & 0xFFU));

        ASSERT_EQ(view.size, expected.size());

        EXPECT_EQ(
            std::memcmp(view.bytes(), expected.data(), expected.size()),
            0);
    }

    EXPECT_TRUE(ring.empty());
}

TEST(JournalRing, SequentialN10000StrictFIFO)
{
    JournalRingBuffer ring;

    constexpr std::size_t kRecords = 10000;

    for (std::size_t i = 0; i < kRecords; ++i) {
        std::array<std::uint8_t, 32> data{};

        for (std::size_t j = 0; j < data.size(); ++j) {
            data[j] = static_cast<std::uint8_t>((i + j) & 0xFFU);
        }

        ASSERT_EQ(
            ring.tryPush(
                data.data(),
                data.size(),
                static_cast<std::uint64_t>(i),
                0,
                1),
            JournalRingBuffer::PushResult::Ok);

        JournalRingBuffer::RecordView view;

        ASSERT_TRUE(ring.tryPop(view));

        ASSERT_EQ(view.size, data.size());

        EXPECT_EQ(
            std::memcmp(view.bytes(), data.data(), data.size()),
            0);
    }

    EXPECT_TRUE(ring.empty());
}

TEST(JournalRing, FullReturnsFull)
{
    JournalRingBuffer ring;

    std::array<std::uint8_t, 32> data{};

    for (std::size_t i = 0;
         i < JournalRingBuffer::kRingCapacity;
         ++i) {

        ASSERT_EQ(
            ring.tryPush(data.data(), data.size()),
            JournalRingBuffer::PushResult::Ok);
    }

    EXPECT_TRUE(ring.full());

    EXPECT_EQ(
        ring.tryPush(data.data(), data.size()),
        JournalRingBuffer::PushResult::Full);
}

TEST(JournalRing, PopReleasesSlotToProducer)
{
    JournalRingBuffer ring;

    std::array<std::uint8_t, 16> first{};
    std::array<std::uint8_t, 16> second{};

    first.fill(0x11);
    second.fill(0x22);

    ASSERT_EQ(
        ring.tryPush(first.data(), first.size()),
        JournalRingBuffer::PushResult::Ok);

    JournalRingBuffer::RecordView view;

    ASSERT_TRUE(ring.tryPop(view));

    EXPECT_EQ(
        std::memcmp(view.bytes(), first.data(), first.size()),
        0);

    ASSERT_EQ(
        ring.tryPush(second.data(), second.size()),
        JournalRingBuffer::PushResult::Ok);

    ASSERT_TRUE(ring.tryPop(view));

    EXPECT_EQ(
        std::memcmp(view.bytes(), second.data(), second.size()),
        0);
}

TEST(JournalRing, SPSCProducerConsumerOrdering)
{
    JournalRingBuffer ring;

    constexpr std::size_t kRecords = 100000;

    std::atomic<bool> producer_done{false};
    std::atomic<bool> failed{false};

    std::thread producer(
        [&] {
            for (std::size_t i = 0; i < kRecords; ++i) {
                std::array<std::uint8_t, 64> data{};

                for (std::size_t j = 0; j < data.size(); ++j) {
                    data[j] = static_cast<std::uint8_t>((i + j) & 0xFFU);
                }

                while (true) {
                    const auto result = ring.tryPush(
                        data.data(),
                        data.size(),
                        static_cast<std::uint64_t>(i),
                        0,
                        1);

                    if (result ==
                        JournalRingBuffer::PushResult::Ok) {
                        break;
                    }

                    if (result ==
                        JournalRingBuffer::PushResult::Invalid) {
                        failed.store(true, std::memory_order_release);
                        return;
                    }

                    std::this_thread::yield();
                }
            }

            producer_done.store(true, std::memory_order_release);
        });

    std::thread consumer(
        [&] {
            std::size_t expected_index = 0;

            while (
                !producer_done.load(std::memory_order_acquire) ||
                !ring.empty()) {

                JournalRingBuffer::RecordView view;

                if (!ring.tryPop(view)) {
                    std::this_thread::yield();
                    continue;
                }

                if (view.size != 64) {
                    failed.store(true, std::memory_order_release);
                    return;
                }

                for (std::size_t j = 0; j < view.size; ++j) {
                    const auto expected =
                        static_cast<std::uint8_t>(
                            (expected_index + j) & 0xFFU);

                    if (view.data[j] != expected) {
                        failed.store(true, std::memory_order_release);
                        return;
                    }
                }

                ++expected_index;
            }

            if (expected_index != kRecords) {
                failed.store(true, std::memory_order_release);
            }
        });

    producer.join();
    consumer.join();

    EXPECT_FALSE(failed.load(std::memory_order_acquire));

    EXPECT_TRUE(ring.empty());
}