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

# Concurrent determinism test
add_executable(run_concurrent_determinism_tests
    tests/test_concurrent_determinism.cpp
    src/ConcurrentMatchingEngine.cpp
    src/RingBuffer.cpp
    src/OrderBook.cpp
    src/MatchingEngine.cpp
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
)

target_link_libraries(run_stress_tests PRIVATE gtest_main Threads::Threads)
target_include_directories(run_stress_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME stress_tests COMMAND run_stress_tests)
set_tests_properties(stress_tests PROPERTIES TIMEOUT 900)

# RiskEngine tests
add_executable(run_riskengine_tests
    tests/test_riskengine.cpp
    src/RiskEngine.cpp
)

target_link_libraries(run_riskengine_tests PRIVATE gtest_main)
target_include_directories(run_riskengine_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_test(NAME riskengine_tests COMMAND run_riskengine_tests)