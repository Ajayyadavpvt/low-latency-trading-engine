// benchmarks/benchmark.cpp
#include "../include/MatchingEngine.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <random>
#include <cstdint>
#include <limits>
#include <exception>

// Helper: interpolated percentile for better accuracy
double percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    double pos = p * (sorted.size() - 1);
    size_t idx = static_cast<size_t>(pos);
    double frac = pos - idx;
    if (idx + 1 < sorted.size()) {
        return sorted[idx] * (1.0 - frac) + sorted[idx + 1] * frac;
    }
    return sorted[idx];
}

int main() {
    try {
        // Parameters
        const int NUM_ORDERS = 100000;          // 1 lakh orders
        const int WARMUP_ORDERS = 10000;        // warm-up (not timed)
        const int NUM_PRICE_TICKS = 50;         // bounded price levels
        const double PRICE_BASE = 95.0;
        const double PRICE_STEP = 0.10;         // 50 ticks -> 5.0 range

        // Random generator (fixed seed for reproducibility)
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> qty_dist(1, 100);
        std::uniform_int_distribution<int> tick_dist(0, NUM_PRICE_TICKS - 1);
        std::uniform_int_distribution<int> side_dist(0, 1);

        auto make_price = [&](int tick) {
            return PRICE_BASE + tick * PRICE_STEP;
        };

        // ---- Warm-up (not timed) ----
        MatchingEngine warm_engine;
        for (int i = 0; i < WARMUP_ORDERS; ++i) {
            OrderSide side = (side_dist(rng) == 0) ? OrderSide::BUY : OrderSide::SELL;
            double price = make_price(tick_dist(rng));
            uint32_t qty = qty_dist(rng);
            Order order(i + 1, (i % 100) + 1, side, OrderType::LIMIT, price, qty);
            warm_engine.processOrder(order);
        }

        // ---- Timed measurement ----
        MatchingEngine engine;
        std::vector<double> latencies;
        latencies.reserve(NUM_ORDERS);

        auto total_start = std::chrono::steady_clock::now();

        for (int i = 0; i < NUM_ORDERS; ++i) {
            OrderSide side = (side_dist(rng) == 0) ? OrderSide::BUY : OrderSide::SELL;
            double price = make_price(tick_dist(rng));
            uint32_t qty = qty_dist(rng);
            Order order(i + 1, (i % 100) + 1, side, OrderType::LIMIT, price, qty);

            auto start = std::chrono::steady_clock::now();
            auto trades = engine.processOrder(order); // trades discarded (measured)
            auto end = std::chrono::steady_clock::now();

            double latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            latencies.push_back(latency_ns);
        }

        auto total_end = std::chrono::steady_clock::now();
        double total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();

        // Throughput with zero-division guard
        double throughput = 0.0;
        if (total_time_ms > 0.0) {
            throughput = NUM_ORDERS / (total_time_ms / 1000.0);
        } else {
            throughput = std::numeric_limits<double>::infinity();
        }

        // Latency statistics
        std::sort(latencies.begin(), latencies.end());
        double avg_latency = 0.0;
        for (double lat : latencies) avg_latency += lat;
        avg_latency /= latencies.size();

        double p50 = percentile(latencies, 0.50);
        double p95 = percentile(latencies, 0.95);
        double p99 = percentile(latencies, 0.99);
        double max_latency = latencies.back();

        // Output
        std::cout << "\n=== Benchmark Results (Bounded Book, Warm-up Done) ===\n";
        std::cout << "Total Orders: " << NUM_ORDERS << "\n";
        std::cout << "Total Time: " << total_time_ms << " ms\n";
        std::cout << "Throughput: " << throughput << " orders/second\n";
        std::cout << "\n--- Latency ---\n";
        std::cout << "Average: " << avg_latency << " ns\n";
        std::cout << "P50 (Median): " << p50 << " ns\n";
        std::cout << "P95: " << p95 << " ns\n";
        std::cout << "P99: " << p99 << " ns\n";
        std::cout << "Max: " << max_latency << " ns\n";
        std::cout << "Final Order Book Size: " << engine.getOrderCount() << "\n";
        std::cout << "=============================================\n\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Benchmark error: " << e.what() << "\n";
        return 1;
    }
}