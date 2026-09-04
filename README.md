# Low-Latency Trading Engine

A high-performance order matching engine built in C++17, designed for learning and demonstrating systems programming concepts used in High-Frequency Trading (HFT) firms like Jane Street and Optiver.

## Overview

This project implements a **price-time priority matching engine** with:
- **Lock-free SPSC ring buffer** for inter-thread communication
- **Multithreaded producer-consumer architecture**
- **Google Test** unit testing (9 tests)
- **Latency benchmarking** with P50/P95/P99 statistics

The engine processes **1.14 million orders/second** with **592ns average latency** on a single thread.

---

## Features

### Order Matching
- Supports `MARKET`, `LIMIT`, `IOC`, `FOK` order types
- Price-time priority (best price first, then earliest order)
- Partial fills and order cancellation
- Real-time best bid/ask tracking

### Concurrency
- Lock-free SPSC (Single Producer Single Consumer) ring buffer
- Atomic operations with explicit memory ordering (acquire/release)
- Producer threads submit orders, consumer thread matches them

### Testing & Benchmarking
- 9 unit tests using Google Test framework
- Throughput measurement (orders/second)
- Latency percentiles: P50, P95, P99, max

---

## Performance

Measured on: Windows 10, GCC 13.3.0 (MinGW-w64), Intel i5 (4 cores)

| Metric | Value |
|--------|-------|
| Throughput | 1,136,000 orders/sec |
| Average Latency | 592 ns |
| P50 (Median) | 500 ns |
| P95 | 1,100 ns |
| P99 | 1,700 ns |
| Max Latency | 96,500 ns (96.5 μs) |

> **Note:** Max latency outlier is caused by `std::map` rebalancing and `std::vector` reallocations. Future optimization will replace these with sorted arrays and memory pools.

---

## Architecture

```
┌─────────────────┐
│  Producer Thread │
│  (Order Source)  │
└────────┬────────┘
         │ push()
         ▼
┌─────────────────┐
│   Ring Buffer   │  ← Lock-free SPSC queue
│  (Fixed Size)   │
└────────┬────────┘
         │ pop()
         ▼
┌─────────────────┐
│ Consumer Thread  │
│ (MatchingEngine) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│    OrderBook    │
│  Bids │  Asks   │
│  (std::map)     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Trades      │
└─────────────────┘
```

---

## Project Structure

```
low-latency-trading-engine/
├── include/
│   ├── Order.h                    # Order struct (id, side, type, price, qty)
│   ├── Trade.h                    # Trade struct (buy_id, sell_id, price, qty)
│   ├── OrderBook.h                # Bids/asks storage + matching logic
│   ├── MatchingEngine.h           # Engine entry point (wrapper)
│   ├── RingBuffer.h               # Lock-free SPSC circular buffer
│   └── ConcurrentMatchingEngine.h # Multithreaded producer-consumer engine
├── src/
│   ├── OrderBook.cpp              # Matching implementation
│   ├── MatchingEngine.cpp         # Engine wrapper implementation
│   ├── RingBuffer.cpp             # Lock-free queue implementation
│   ├── ConcurrentMatchingEngine.cpp
│   └── main.cpp                   # Basic usage example
├── tests/
│   ├── test_matching.cpp          # 5 matching engine tests
│   ├── test_ringbuffer.cpp        # 3 ring buffer tests
│   └── test_concurrent.cpp        # 1 concurrent engine test
├── benchmarks/
│   └── benchmark.cpp              # Latency & throughput measurement
├── CMakeLists.txt                 # Build system
└── README.md
```

---

## Design Decisions

| Decision | Reason |
|----------|--------|
| `struct` instead of `class` | No invariants, direct member access for speed |
| `uint64_t` / `uint32_t` | Fixed-width integers for predictable memory layout |
| `std::map` for bids/asks | Automatic sorting by price (O(log n) access) |
| `std::vector<Order>` per price level | Time priority within same price |
| Power-of-2 ring buffer size | Bitwise AND (`& mask`) instead of modulo (`%`) |
| `memory_order_acquire/release` | Correct visibility without full fence overhead |
| One slot sacrificed in ring buffer | Simplifies full vs empty detection |

---

## Build

### Prerequisites
- CMake 3.14+
- GCC 13+ (or MSVC 2019+)
- Internet (first build downloads Google Test)

### Build Commands
```bash
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

### Run Tests
```bash
./run_tests.exe              # Matching engine tests (5 tests)
./run_ringbuffer_tests.exe   # Lock-free queue tests (3 tests)
./run_concurrent_tests.exe   # Multithreaded tests (1 test)
```

### Run Benchmark
```bash
./benchmark.exe
```

---

## Usage Example

```cpp
#include "MatchingEngine.h"
#include "Order.h"

int main() {
    MatchingEngine engine;

    // Place a limit buy order: 100 shares @ ₹100.50
    Order buy(1, 1001, OrderSide::BUY, OrderType::LIMIT, 100.50, 100);
    engine.processOrder(buy);

    // Place a limit sell order: 50 shares @ ₹100.50 (matches)
    Order sell(2, 2001, OrderSide::SELL, OrderType::LIMIT, 100.50, 50);
    auto trades = engine.processOrder(sell);

    // trades[0] = Trade(quantity=50, price=100.50, buy_id=1, sell_id=2)
    return 0;
}
```

---

## Future Improvements

- [ ] **MPMC Queue**: Support multiple producers/consumers
- [ ] **Memory Pool**: Pre-allocated Order objects to eliminate dynamic allocation
- [ ] **Sorted Arrays**: Replace `std::map` with cache-friendly sorted vectors
- [ ] **Network Layer**: TCP/UDP order feed using Boost.Asio
- [ ] **Multi-Symbol**: Support multiple instruments simultaneously
- [ ] **Risk Checks**: Order validation, position limits
- [ ] **Persistence**: Save trades to database/file

---

## License

MIT

## Author

Ajay Yadav — [GitHub](https://github.com/Rareajayyadav)

---

*Built as a learning project to demonstrate C++ systems programming, lock-free data structures, and performance engineering.*