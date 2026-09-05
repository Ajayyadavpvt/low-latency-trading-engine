# Low-Latency Trading Engine

A high-performance order matching engine built in C++17, designed for learning and demonstrating systems programming concepts used in High-Frequency Trading (HFT) firms like Jane Street and Optiver.

## Overview

This project implements a **price-time priority matching engine** with:
- **Lock-free SPSC ring buffer** for inter-thread communication
- **Multithreaded producer-consumer architecture**
- **Self-trade prevention (STP)** with configurable policies
- **Google Test** unit testing (21 tests)
- **Latency benchmarking** with P50/P95/P99 statistics

The engine processes **1.41 million orders/second** with **528ns average latency** on a single thread.

---

## Features

### Order Matching
- Supports `MARKET`, `LIMIT`, `IOC`, `FOK` order types
- Price-time priority (best price first, then earliest order)
- Partial fills and order cancellation
- FOK atomicity guarantee (fully fill or nothing)
- Real-time best bid/ask tracking
- Input validation (zero qty, zero IDs, negative price, NaN/Inf)

### Self-Trade Prevention (STP)
- `NONE` — no prevention (default)
- `CANCEL_NEWEST` — cancel incoming order
- `CANCEL_OLDEST` — cancel resting order
- `CANCEL_BOTH` — cancel both orders

### Concurrency
- Lock-free SPSC (Single Producer Single Consumer) ring buffer
- Atomic operations with explicit memory ordering (acquire/release)
- Cache-line padding (`alignas(64)`) to avoid false sharing
- Producer threads submit orders, consumer thread matches them
- Drain-on-stop guarantee — no submitted order is silently dropped

### Testing & Benchmarking
- 21 unit tests using Google Test framework
- Determinism test (same input → same output, 2x)
- Bounded book depth benchmark with warm-up phase
- Latency percentiles: P50, P95, P99, max (interpolated)

---

## Performance

Measured on: Windows 10, GCC 13.3.0 (MinGW-w64)

| Metric | Value |
|--------|-------|
| Throughput | 1,408,450 orders/sec |
| Average Latency | 528 ns |
| P50 (Median) | 400 ns |
| P95 | 1,200 ns |
| P99 | 2,200 ns |
| Max Latency | 96,100 ns (96.1 μs) |

> **Note:** Max latency outlier is caused by `std::map` tree rebalancing. Phase 8 will replace this with sorted arrays for better cache locality and consistent tail latency.

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
│   Ring Buffer   │  ← Lock-free SPSC queue (alignas(64))
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
│   ├── Order.h                    # Order struct + validation
│   ├── Trade.h                    # Trade struct (with trader IDs for audit)
│   ├── OrderBook.h                # Bids/asks + STP policy
│   ├── MatchingEngine.h           # Single-threaded engine wrapper
│   ├── RingBuffer.h               # Lock-free SPSC queue (cache-aligned)
│   └── ConcurrentMatchingEngine.h # Multithreaded producer-consumer
├── src/
│   ├── OrderBook.cpp              # Matching + STP + FOK logic
│   ├── MatchingEngine.cpp         # Wrapper implementation
│   ├── RingBuffer.cpp             # Lock-free queue implementation
│   ├── ConcurrentMatchingEngine.cpp # Drain-on-stop, CAS, thread-safe stop
│   └── main.cpp                   # Basic usage demo
├── tests/
│   ├── test_matching.cpp          # 15 matching + STP + determinism tests
│   ├── test_ringbuffer.cpp        # 5 ring buffer tests (wraparound, threads)
│   └── test_concurrent.cpp        # 1 concurrent processing test
├── benchmarks/
│   └── benchmark.cpp              # Bounded-book benchmark with warm-up
├── CMakeLists.txt                 # Build system (FetchContent for gtest)
└── README.md
```

---

## Design Decisions

| Decision | Reason |
|----------|--------|
| `struct` instead of `class` for Order/Trade | No invariants, direct member access for speed |
| `uint64_t` / `uint32_t` fixed-width integers | Predictable memory layout across platforms |
| `std::map` for bids/asks | Automatic sorting by price (Phase 8: replace with sorted vector) |
| `std::vector<Order>` per price level | Time priority within same price |
| `steady_clock` not `high_resolution_clock` | Guaranteed monotonic — no NTP jumps |
| `alignas(64)` on ring buffer atomics | Avoid false sharing between producer/consumer |
| Power-of-2 ring buffer size | Bitwise AND (`& mask`) instead of modulo (`%`) |
| `memory_order_acquire/release` | Correct visibility without full fence overhead |
| Validation with `throw invalid_argument` | Active in release build (unlike `assert`) |
| Default constructor for Order | Placeholder for ring buffer, validation-free |

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
./run_tests.exe              # Matching engine + STP + determinism (15 tests)
./run_ringbuffer_tests.exe   # Lock-free queue (5 tests)
./run_concurrent_tests.exe   # Multithreaded engine (1 test)
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
    engine.setSTPPolicy(STPPolicy::CANCEL_NEWEST);

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

## Phase 7 Changelog (Current)

- Fixed `static trade_id` bug (class member, no cross-instance ID collision)
- Added input validation (zero qty/IDs, negative/NaN/Inf price for LIMIT/IOC/FOK)
- Implemented Self-Trade Prevention (3 policies: CANCEL_NEWEST, CANCEL_OLDEST, CANCEL_BOTH)
- Fixed FOK atomicity — `canFullyFill()` pre-check with self-trade exclusion
- Added determinism test — same input sequence produces identical trades
- Fixed RingBuffer `assert` → runtime exception (release-safe power-of-2 validation)
- Added `alignas(64)` to ring buffer atomics (false sharing eliminated)
- Fixed `ConcurrentMatchingEngine::stop()` to drain pending orders before joining
- Added CAS (`compare_exchange_strong`) for start/stop thread-safety
- Benchmark: bounded book depth with warm-up phase, interpolated percentiles

## Future Improvements

- [ ] **Phase 8**: Replace `std::map` with sorted arrays, memory pool, cache alignment
- [ ] **Phase 9**: Concurrent pipeline determinism, sanitizer CI, stress tests
- [ ] **Phase 10**: Network layer (TCP server, binary protocol)
- [ ] **Phase 11**: Multi-symbol support, risk checks
- [ ] **Phase 12**: Profiling report, release

---

## License

MIT

## Author

Ajay Yadav — [GitHub](https://github.com/Rareajayyadav)

---

*Built as a learning project to demonstrate C++ systems programming, lock-free data structures, and performance engineering.*