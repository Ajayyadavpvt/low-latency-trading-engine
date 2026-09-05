// include/Order.h
#ifndef ORDER_H
#define ORDER_H

#include <cstdint>
#include <string>
#include <chrono>
#include <stdexcept>
#include <cmath>

enum class OrderSide { BUY, SELL };
enum class OrderType { MARKET, LIMIT, IOC, FOK };

struct Order {
    uint64_t order_id;
    uint64_t trader_id;
    OrderSide side;
    OrderType type;
    double price;
    uint32_t quantity;
    uint32_t remaining_quantity;
    std::chrono::nanoseconds timestamp;
    std::chrono::nanoseconds received_time;

    // Default constructor (for std::vector<Order> inside RingBuffer).
    // Creates a placeholder object — validation is NOT applied here
    // because containers need to default-construct empty slots.
    Order()
        : order_id(0), trader_id(0), side(OrderSide::BUY), type(OrderType::MARKET),
          price(0.0), quantity(0), remaining_quantity(0),
          timestamp(std::chrono::steady_clock::now().time_since_epoch()),
          received_time(timestamp) {}

    // Parameterized constructor — full validation.
    // Throws std::invalid_argument on invalid input.
    Order(uint64_t oid, uint64_t tid, OrderSide s, OrderType t,
          double p, uint32_t qty)
        : order_id(oid), trader_id(tid), side(s), type(t), price(p),
          quantity(qty), remaining_quantity(qty),
          timestamp(std::chrono::steady_clock::now().time_since_epoch()),
          received_time(timestamp) {

        if (qty == 0) {
            throw std::invalid_argument("Order quantity must be positive");
        }
        if (oid == 0) {
            throw std::invalid_argument("Order ID must be non-zero");
        }
        if (tid == 0) {
            throw std::invalid_argument("Trader ID must be non-zero");
        }

        if (t == OrderType::MARKET) {
            price = 0.0;
        } else {
            if (p <= 0.0 || std::isnan(p) || std::isinf(p)) {
                throw std::invalid_argument("Limit/IOC/FOK order price must be positive and finite");
            }
        }
    }

    bool is_filled() const {
        return remaining_quantity == 0;
    }

    std::string to_string() const {
        std::string side_str = (side == OrderSide::BUY) ? "BUY" : "SELL";
        return "Order[" + std::to_string(order_id) + "] " +
               side_str + " " + std::to_string(remaining_quantity) +
               "@" + std::to_string(price);
    }
};

#endif // ORDER_H