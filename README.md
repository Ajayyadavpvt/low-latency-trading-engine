# Low-Latency Trading Engine

A high-performance, deterministic order matching engine built in C++17, designed for HFT (High-Frequency Trading) systems and financial infrastructure.

## Overview

This project implements a **price-time priority matching engine** with:
- **Lock-free SPSC ring buffer** for inter-thread communication
- **Multi-symbol sharding** with per-shard independent matching engines
- **Self-trade prevention (STP)** with configurable policies
- **Memory pool** and **OrderQueue** for zero-allocation hot paths
- **Async binary trade persistence** via TradeLogger
- **Google Test** unit testing (30 tests)
- **CI/CD** with AddressSanitizer and ThreadSanitizer
- **Latency benchmarking** with P50/P95/P99 statistics

The engine processes **1.5M+ orders/sec** with **sub-microsecond average latency** on a single thread.

---

## Features

### Order Matching
- Supports `MARKET`, `LIMIT`, `IOC`, `FOK` order types
- Price-time priority (best price first, then earliest order)
- Partial fills and order cancellation
- FOK atomicity guarantee (fully fill or nothing)
- Real-time best bid/ask tracking
- Input validation (zero qty, negative price, NaN/Inf)

### Self-Trade Prevention (STP)
- `NONE` — no prevention (default)
- `CANCEL_NEWEST` — cancel incoming order
- `CANCEL_OLDEST` — cancel resting order
- `CANCEL_BOTH` — cancel both orders

### Concurrency
- Lock-free SPSC (Single Producer Single Consumer) ring buffer
- Atomic operations with explicit memory ordering (acquire/release)
- Cache-line padding (`alignas(64)`) to avoid false sharing
- Producer-consumer model with drain-on-stop guarantee
- CAS-based start/stop for thread safety

### Multi-Symbol Sharding
- `SymbolId` as integer (not string) for O(1) hot-path routing
- `SymbolTable` with precomputed symbol→shard mapping
- `ShardedMatchingEngine` with per-shard independent engine instances
- Deterministic per-symbol state isolation

### Memory Management
- `OrderPool` — pre-allocated storage with double-free detection
- `OrderQueue` — vector-backed queue with head-index and capacity-triggered compaction
- Sorted `std::vector<PriceLevel>` for cache-friendly price levels

### Persistence
- `TradeLogger` — async binary trade logging with bounded queue
- Write-ahead log semantics for audit trail
- Thread-safe, non-blocking on hot path

### Testing & Benchmarking
- 30 unit tests using Google Test
- Determinism tests (single-threaded + concurrent consistency)
- Stress tests (1M orders, multi-producer, backpressure)
- Bounded book depth benchmark with warm-up phase
- Latency percentiles: P50, P95, P99, max

---

## Performance

Measured on: Windows 10, GCC 13.3.0 (MinGW-w64)

| Metric | Value |
|--------|-------|
| Throughput | 1,500,000+ orders/sec (single-threaded) |
| Average Latency | ~500 ns |
| P50 | ~400 ns |
| P95 | ~1000 ns |
| P99 | ~1700 ns |
| Max | ~37 μs |

> **Note:** Max latency outlier was reduced from 96μs (std::map) to 37μs (sorted vector). Further tail-latency improvements planned with profiling (Phase 12).

---

## Architecture

```
┌─────────────────────┐
│   Producer Thread   │
│   (Order Source)    │
└──────────┬──────────┘
           │ submitOrder()
           ▼
┌─────────────────────┐
│  ShardedMatching    │
│     Engine          │
│  ┌───┐ ┌───┐ ┌───┐ │
│  │S0 │ │S1 │ │S2 │ │  ← per-symbol shards
│  └─┬─┘ └─┬─┘ └─┬─┘ │
└────┼──────┼──────┼──┘
     ▼      ▼      ▼
┌─────────┐┌─────────┐┌─────────┐
│Consumer ││Consumer ││Consumer │
│Thread 0 ││Thread 1 ││Thread 2 │
└────┬────┘└────┬────┘└────┬────┘
     ▼          ▼          ▼
┌─────────┐┌─────────┐┌─────────┐
│OrderBook││OrderBook││OrderBook│
│ (Shard) ││ (Shard) ││ (Shard) │
└────┬────┘└────┬────┘└────┬────┘
     │          │          │
     ▼          ▼          ▼
   Trades    Trades     Trades
     │          │          │
     └──────────┼──────────┘
                ▼
        TradeLogger (async binary)
```

---

## Project Structure

```
low-latency-trading-engine/
├── include/
│   ├── Order.h                    # Order struct + validation
│   ├── Trade.h                    # Trade struct (with trader IDs)
│   ├── SymbolId.h                 # Integer symbol IDs + SymbolTable
│   ├── OrderBook.h                # Bids/asks + STP policy
│   ├── OrderQueue.h               # Vector-backed queue (O(1) pop)
│   ├── OrderPool.h                # Pre-allocated order storage
│   ├── RingBuffer.h               # Lock-free SPSC queue
│   ├── MatchingEngine.h           # Single-threaded matching wrapper
│   ├── ConcurrentMatchingEngine.h # Producer-consumer engine
│   ├── ShardedEngine.h            # Multi-symbol sharding
│   └── TradeLogger.h              # Async binary trade persistence
├── src/
│   ├── OrderBook.cpp              # Matching + STP + FOK logic
│   ├── MatchingEngine.cpp         # Wrapper implementation
│   ├── RingBuffer.cpp             # Lock-free queue implementation
│   ├── ConcurrentMatchingEngine.cpp # Thread-safe producer-consumer
│   ├── ShardedEngine.cpp          # Symbol routing + shard management
│   ├── TradeLogger.cpp            # Async binary logger
│   └── main.cpp                   # Basic usage demo
├── tests/
│   ├── test_matching.cpp          # 15 matching + STP + determinism
│   ├── test_ringbuffer.cpp        # 5 ring buffer tests
│   ├── test_concurrent.cpp        # 1 concurrent processing
│   ├── test_tradelogger.cpp       # 2 logger tests
│   ├── test_concurrent_determinism.cpp # 2 determinism tests
│   ├── test_symbolid.cpp          # 5 symbol table tests
│   ├── test_sharded.cpp           # 2 sharded engine tests
│   └── test_stress.cpp            # 3 stress tests
├── benchmarks/
│   └── benchmark.cpp              # Bounded-book benchmark
├── .github/workflows/
│   └── ci.yml                     # ASan/TSan CI pipeline
├── CMakeLists.txt                 # Build system
└── README.md
```

---

## Design Decisions

| Decision | Reason |
|----------|--------|
| `struct` instead of `class` for Order/Trade | No invariants, direct member access for speed |
| `uint64_t` / `uint32_t` fixed-width integers | Predictable memory layout across platforms |
| Sorted `std::vector<PriceLevel>` | Better cache locality than `std::map`, no tree rebalancing |
| `OrderQueue` (head-index vector) | O(1) pop_front, O(1) amortized push, contiguous memory |
| `OrderPool` pre-allocated storage | Zero dynamic allocation on hot path |
| `steady_clock` not `high_resolution_clock` | Guaranteed monotonic — no NTP jumps |
| `alignas(64)` on ring buffer atomics | Avoid false sharing between producer/consumer |
| Power-of-2 ring buffer size | Bitwise AND (`& mask`) instead of modulo (`%`) |
| `memory_order_acquire/release` | Correct visibility without full fence overhead |
| Validation with `throw invalid_argument` | Active in release build (unlike `assert`) |
| Sharding over MPMC | Per-symbol determinism, cache locality, no contention |
| `SymbolId` integer | No string hashing on hot path |
| `SymbolTable.freeze()` | Prevents registration after workers start |

---

## Backpressure Policy

- `RingBuffer::push` returns `false` when buffer is full.
- `ConcurrentMatchingEngine::submitOrder` propagates this `false` — it never silently drops an order.
- The producer decides whether to retry, back off, or reject the order.
- `TradeLogger::log` returns `false` when its bounded queue is full or an error occurred.
- The matching engine continues processing even if trade logging fails, but an error flag is set.
- No operation on the hot path blocks indefinitely.

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
./run_tests.exe                      # matching engine (15)
./run_ringbuffer_tests.exe           # ring buffer (5)
./run_concurrent_tests.exe           # concurrent (1)
./run_tradelogger_tests.exe          # logger (2)
./run_concurrent_determinism_tests.exe # determinism (2)
./run_symbolid_tests.exe             # symbol table (5)
./run_sharded_tests.exe              # sharded engine (2)
./run_stress_tests.exe               # stress (3)
```

### Run Benchmark
```bash
./benchmark.exe
```

---

## Usage Example

```cpp
#include "ShardedEngine.h"
#include "Order.h"

int main() {
    ShardedMatchingEngine engine(4, 1024); // 4 shards, 1024 buffer

    auto& table = engine.getSymbolTable();
    SymbolId btc = table.registerSymbol("BTCUSD");
    SymbolId eth = table.registerSymbol("ETHUSD");
    table.freeze();

    engine.startAll();

    Order btc_buy(1, 100, OrderSide::BUY, OrderType::LIMIT, 100.0, 10, btc);
    bool accepted = engine.submitOrder(btc_buy);

    engine.stopAll();
    return 0;
}
```

---

## Phase Progress

| Phase | Status |
|-------|--------|
| 1: Core Engine | ✅ Complete |
| 2: Build & Test | ✅ Complete |
| 3: Benchmarking | ✅ Complete |
| 4: Ring Buffer | ✅ Complete |
| 5: Multithreading | ✅ Complete |
| 6: Documentation | ✅ Complete |
| 7: Code Hardening | ✅ Complete |
| 8: Performance (sorted vector, OrderPool, async persistence) | ✅ Complete |
| 9: Concurrency (sharding, determinism, stress tests) | ✅ In Progress |
| 10: Multi-Symbol + Risk Checks | ⏳ Planned |
| 11: Network Layer | ⏳ Planned |
| 12: Profiling & Release | ⏳ Planned |

---

## License

MIT

## Author

Ajay Yadav — [GitHub](https://github.com/ajayyadavpvt)

---

*Built as a learning project to demonstrate C++ systems programming, lock-free data structures, sharding, determinism, and performance engineering.*