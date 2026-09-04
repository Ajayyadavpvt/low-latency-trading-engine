# Low-Latency Trading Engine in C++

A high-performance order matching engine with lock-free concurrency, designed for HFT (High-Frequency Trading) systems.

## Features

- **Price-Time Priority Matching**: Bids sorted descending, asks ascending; earliest order gets priority at same price.
- **Order Types**: MARKET, LIMIT, IOC, FOK
- **Lock-Free SPSC Ring Buffer**: Single Producer Single Consumer queue using atomic operations.
- **Multithreaded Architecture**: Producer threads submit orders, consumer thread matches them.
- **Unit Tests**: Google Test framework with 5 matching tests, 3 ring buffer tests, 1 concurrent test.
- **Benchmarking**: Latency (P50/P95/P99) and throughput measurement.

## Performance

- **Throughput**: 1.14 million orders/second (single-threaded)
- **Average Latency**: 592 ns
- **P99 Latency**: 1.7 μs

## Project Structure
low-latency-trading-engine/
├── include/ # Header files
├── src/ # Implementation files
├── tests/ # Unit tests (Google Test)
├── benchmarks/ # Performance benchmarks
├── CMakeLists.txt # Build system
└── README.md


## Build & Run

### Prerequisites
- CMake 3.14+
- GCC 13+ (or any C++17 compiler)
- Google Test (auto-downloaded by CMake)

### Build
```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build .