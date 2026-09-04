// include/Order.h
#ifndef ORDER_H
#define ORDER_H

#include <cstdint>
#include <string>
#include <chrono>

// OrderSide: BUY or SELL
enum class OrderSide {
    BUY,
    SELL
};

// OrderType: Different types of orders
enum class OrderType {
    MARKET,     // Execute immediately at best available price
    LIMIT,      // Execute at specified price or better
    IOC,        // Immediate or Cancel: partially or fully fill immediately, cancel rest
    FOK         // Fill or Kill: fill completely or cancel entirely
};

// struct used instead of class for performance (direct member access)
struct Order {
    // --- Identification ---
    uint64_t order_id;   // Unique order ID (fixed width, fast)
    uint64_t trader_id;  // ID of the trader who placed the order

    // --- Order Details ---
    OrderSide side;      // BUY or SELL
    OrderType type;      // MARKET, LIMIT, IOC, FOK
    double price;        // Limit price (0 for MARKET orders)
    uint32_t quantity;   // Original quantity
    uint32_t remaining_quantity; // Quantity remaining after partial fills

    // --- Timestamps (for latency measurement) ---
    std::chrono::nanoseconds timestamp;       // Order creation time
    std::chrono::nanoseconds received_time;   // Time when engine received it

    // Default constructor (required for std::vector)
    Order()
        : order_id(0)
        , trader_id(0)
        , side(OrderSide::BUY)
        , type(OrderType::MARKET)
        , price(0.0)
        , quantity(0)
        , remaining_quantity(0)
        , timestamp(std::chrono::high_resolution_clock::now().time_since_epoch())
        , received_time(timestamp) {}

    // Parameterized constructor
    Order(uint64_t oid, uint64_t tid, OrderSide s, OrderType t, 
          double p, uint32_t qty)
        : order_id(oid)
        , trader_id(tid)
        , side(s)
        , type(t)
        , price(p)
        , quantity(qty)
        , remaining_quantity(qty) // initially all quantity is pending
        , timestamp(std::chrono::high_resolution_clock::now().time_since_epoch())
        , received_time(timestamp) {}

    // Helper: check if order is fully filled
    bool is_filled() const {
        return remaining_quantity == 0;
    }

    // Helper: for debugging
    std::string to_string() const {
        std::string side_str = (side == OrderSide::BUY) ? "BUY" : "SELL";
        return "Order[" + std::to_string(order_id) + "] " + 
               side_str + " " + std::to_string(remaining_quantity) + 
               "@" + std::to_string(price);
    }
};

#endif // ORDER_H