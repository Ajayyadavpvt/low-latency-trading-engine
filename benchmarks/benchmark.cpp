// benchmarks/benchmark.cpp
#include "../include/MatchingEngine.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <random>

// Measure latency and throughput of matching engine
int main() {
    MatchingEngine engine;
    
    // Parameters
    const int NUM_ORDERS = 100000;  // 1 lakh orders
    const double PRICE_RANGE = 100.0;
    
    // Random number generator
    std::mt19937 rng(42); // fixed seed for reproducibility
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_real_distribution<double> price_dist(95.0, 105.0);
    std::uniform_int_distribution<int> side_dist(0, 1);
    
    // Store latency measurements
    std::vector<double> latencies;
    latencies.reserve(NUM_ORDERS);
    
    // Start total time measurement
    auto total_start = std::chrono::high_resolution_clock::now();
    
    // Process orders
    for (int i = 0; i < NUM_ORDERS; ++i) {
        OrderSide side = (side_dist(rng) == 0) ? OrderSide::BUY : OrderSide::SELL;
        double price = price_dist(rng);
        uint32_t qty = qty_dist(rng);
        
        Order order(i+1, (i % 100) + 1, side, OrderType::LIMIT, price, qty);
        
        // Measure latency for this order
        auto start = std::chrono::high_resolution_clock::now();
        auto trades = engine.processOrder(order);
        auto end = std::chrono::high_resolution_clock::now();
        
        double latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        latencies.push_back(latency_ns);
    }
    
    auto total_end = std::chrono::high_resolution_clock::now();
    double total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
    
    // Calculate throughput
    double throughput = NUM_ORDERS / (total_time_ms / 1000.0); // orders per second
    
    // Calculate latency statistics
    std::sort(latencies.begin(), latencies.end());
    
    double avg_latency = 0;
    for (double lat : latencies) avg_latency += lat;
    avg_latency /= latencies.size();
    
    double p50 = latencies[latencies.size() / 2];
    double p95 = latencies[(size_t)(latencies.size() * 0.95)];
    double p99 = latencies[(size_t)(latencies.size() * 0.99)];
    double max_latency = latencies.back();
    
    // Print results
    std::cout << "\n=== Benchmark Results ===\n";
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
    std::cout << "=======================\n\n";
    
    return 0;
}