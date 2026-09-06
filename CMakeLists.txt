cmake_minimum_required(VERSION 3.14)
project(LowLatencyTradingEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

# Main executable
add_executable(trading_engine
    src/OrderBook.cpp
    src/MatchingEngine.cpp
    src/main.cpp
)

# Enable testing
enable_testing()

# Google Test - fetch from GitHub
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
FetchContent_MakeAvailable(googletest)

# Test executable
add_executable(run_tests
    tests/test_matching.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
)

target_link_libraries(run_tests gtest_main)
add_test(NAME matching_tests COMMAND run_tests)

# Benchmark executable
add_executable(benchmark
    benchmarks/benchmark.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
)

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
)

target_link_libraries(run_concurrent_tests gtest_main)
add_test(NAME concurrent_tests COMMAND run_concurrent_tests)
# TradeLogger test executable
find_package(Threads REQUIRED)

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

find_package(Threads REQUIRED)

add_executable(run_concurrent_determinism_tests
    tests/test_concurrent_determinism.cpp
    src/ConcurrentMatchingEngine.cpp
    src/RingBuffer.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
)

target_link_libraries(run_concurrent_determinism_tests
    PRIVATE
    gtest_main
    Threads::Threads
)

target_include_directories(run_concurrent_determinism_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME concurrent_determinism_tests COMMAND run_concurrent_determinism_tests)