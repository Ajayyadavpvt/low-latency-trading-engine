// include/OrderBook.h
#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <map>
#include <vector>
#include <memory>
#include "Order.h"
#include "Trade.h"

// OrderBook maintains buy and sell orders in price-time priority.
// Bids (buy orders) are sorted descending by price (best bid = highest price).
// Asks (sell orders) are sorted ascending by price (best ask = lowest price).
// If two orders have same price, earlier order gets priority (time priority).
class OrderBook {
public:
    // Add a new order to the book
    void addOrder(const Order& order);

    // Cancel an existing order by its ID
    // Returns true if order was found and canceled, false otherwise
    bool cancelOrder(uint64_t order_id);

    // Match an incoming order against existing orders
    // Returns a vector of trades generated
    std::vector<Trade> matchOrder(Order& incoming);

    // Get current best bid price (highest buy price)
    // Returns 0 if no bids exist
    double getBestBid() const;

    // Get current best ask price (lowest sell price)
    // Returns 0 if no asks exist
    double getBestAsk() const;

    // Get number of orders in the book (total)
    size_t getOrderCount() const;

    // Print the order book for debugging (optional)
    void printBook() const;

private:
    // Bids: price -> list of orders at that price (orders in time priority)
    std::map<double, std::vector<Order>, std::greater<double>> bids_;

    // Asks: price -> list of orders at that price (orders in time priority)
    std::map<double, std::vector<Order>, std::less<double>> asks_;

    // Helper: Generate trade between buy and sell orders
    Trade createTrade(const Order& buy, const Order& sell, uint32_t qty, double price);
};

#endif // ORDERBOOK_H