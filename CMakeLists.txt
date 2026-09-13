cmake_minimum_required(VERSION 3.14)
project(LowLatencyTradingEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)

# Main executable
add_executable(trading_engine
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/MarketDataPublisher.cpp
    src/main.cpp
)
target_link_libraries(trading_engine PRIVATE Threads::Threads)

# Enable testing
enable_testing()

# Google Test - fetch from GitHub
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(run_tests
    tests/test_matching.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/MarketDataPublisher.cpp
)
target_link_libraries(run_tests PRIVATE gtest_main Threads::Threads)
add_test(NAME matching_tests COMMAND run_tests)

# Benchmark executable
add_executable(benchmark
    benchmarks/benchmark.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/MarketDataPublisher.cpp
)
target_link_libraries(benchmark PRIVATE Threads::Threads)

# Ring buffer test executable
add_executable(run_ringbuffer_tests
    tests/test_ringbuffer.cpp
    src/RingBuffer.cpp
)
target_link_libraries(run_ringbuffer_tests gtest_main)
add_test(NAME ringbuffer_tests COMMAND run_ringbuffer_tests)

# Concurrent engine test
add_executable(run_concurrent_tests
    tests/test_concurrent.cpp
    src/ConcurrentMatchingEngine.cpp
    src/RingBuffer.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/MarketDataPublisher.cpp
)
target_link_libraries(run_concurrent_tests PRIVATE gtest_main Threads::Threads)
add_test(NAME concurrent_tests COMMAND run_concurrent_tests)

# TradeLogger test executable
add_executable(run_tradelogger_tests
    tests/test_tradelogger.cpp
    src/TradeLogger.cpp
)
target_link_libraries(run_tradelogger_tests PRIVATE
    gtest_main
    Threads::Threads
)
target_compile_features(run_tradelogger_tests PRIVATE cxx_std_17)
add_test(NAME tradelogger_tests COMMAND run_tradelogger_tests)

# Concurrent determinism test
add_executable(run_concurrent_determinism_tests
    tests/test_concurrent_determinism.cpp
    src/ConcurrentMatchingEngine.cpp
    src/RingBuffer.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/MarketDataPublisher.cpp
)
target_link_libraries(run_concurrent_determinism_tests PRIVATE
    gtest_main
    Threads::Threads
)
target_include_directories(run_concurrent_determinism_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME concurrent_determinism_tests COMMAND run_concurrent_determinism_tests)

# Symbol ID test
add_executable(run_symbolid_tests
    tests/test_symbolid.cpp
)
target_link_libraries(run_symbolid_tests gtest_main)
add_test(NAME symbolid_tests COMMAND run_symbolid_tests)

# Sharded engine test
add_executable(run_sharded_tests
    tests/test_sharded.cpp
    src/ShardedEngine.cpp
    src/ConcurrentMatchingEngine.cpp
    src/RingBuffer.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/MarketDataPublisher.cpp
)
target_link_libraries(run_sharded_tests PRIVATE gtest_main Threads::Threads)
target_include_directories(run_sharded_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME sharded_tests COMMAND run_sharded_tests)

# Stress tests
add_executable(run_stress_tests
    tests/test_stress.cpp
    src/ShardedEngine.cpp
    src/ConcurrentMatchingEngine.cpp
    src/RingBuffer.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/MarketDataPublisher.cpp
)
target_link_libraries(run_stress_tests PRIVATE gtest_main Threads::Threads)
target_include_directories(run_stress_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME stress_tests COMMAND run_stress_tests)
set_tests_properties(stress_tests PROPERTIES TIMEOUT 900)

# ------------------------------------------------------------
# RiskEngine tests
# ------------------------------------------------------------
# NOTE: src/OrderBook.cpp added because checkInvariant() tests
#       construct an OrderBook and call addOrder/cancelOrder/
#       getOrderCount/getOrderById — the linker needs these symbols.
# ------------------------------------------------------------
add_executable(run_riskengine_tests
    tests/test_riskengine.cpp
    src/RiskEngine.cpp
    src/OrderBook.cpp
)
target_link_libraries(run_riskengine_tests PRIVATE gtest_main)
target_include_directories(run_riskengine_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME riskengine_tests COMMAND run_riskengine_tests)

# Market data publisher test
add_executable(run_marketdata_tests
    tests/test_marketdata.cpp
    src/MarketDataPublisher.cpp
)
target_link_libraries(run_marketdata_tests PRIVATE gtest_main Threads::Threads)
target_include_directories(run_marketdata_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(run_marketdata_tests PRIVATE cxx_std_17)
add_test(NAME marketdata_tests COMMAND run_marketdata_tests)

# Journal/Recovery test
add_executable(run_journal_tests
    tests/test_journal.cpp
    src/Journal.cpp
    src/JournalRingBuffer.cpp
    src/Recovery.cpp
    src/MatchingEngine.cpp
    src/OrderBook.cpp
    src/MarketDataPublisher.cpp
)
target_link_libraries(run_journal_tests PRIVATE gtest_main Threads::Threads)
target_include_directories(run_journal_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(run_journal_tests PRIVATE cxx_std_17)
add_test(NAME journal_tests COMMAND run_journal_tests)

# Sharded Journal Manager test
add_executable(run_sharded_journal_tests
    tests/test_sharded_journal.cpp
    src/ShardedJournalManager.cpp
    src/Journal.cpp
    src/JournalRingBuffer.cpp
    src/Recovery.cpp
    src/MatchingEngine.cpp
    src/OrderBook.cpp
    src/MarketDataPublisher.cpp
)
target_link_libraries(run_sharded_journal_tests PRIVATE gtest_main Threads::Threads)
target_include_directories(run_sharded_journal_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(run_sharded_journal_tests PRIVATE cxx_std_17)
add_test(NAME sharded_journal_tests COMMAND run_sharded_journal_tests)

# ------------------------------------------------------------
# Feature F: Inline-slot Journal Ring Buffer tests
# ------------------------------------------------------------

add_executable(run_journal_ring_tests
    tests/test_journal_ring.cpp
    src/JournalRingBuffer.cpp
)

target_link_libraries(run_journal_ring_tests PRIVATE
    gtest_main
    Threads::Threads
)

target_include_directories(run_journal_ring_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(run_journal_ring_tests PRIVATE
    cxx_std_17
)

add_test(
    NAME journal_ring_tests
    COMMAND run_journal_ring_tests
)