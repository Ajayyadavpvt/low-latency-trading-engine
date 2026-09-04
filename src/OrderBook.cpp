// src/OrderBook.cpp
#include "../include/OrderBook.h"
#include <iostream>
#include <algorithm>   // for std::min

// Add a new order to the book
void OrderBook::addOrder(const Order& order) {
    if (order.side == OrderSide::BUY) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }
}

// Cancel an order by its ID
bool OrderBook::cancelOrder(uint64_t order_id) {
    // Search in bids
    for (auto& entry : bids_) {
        double price = entry.first;
        auto& orders = entry.second;
        for (auto it = orders.begin(); it != orders.end(); ++it) {
            if (it->order_id == order_id) {
                orders.erase(it);
                if (orders.empty()) {
                    bids_.erase(price);
                }
                return true;
            }
        }
    }
    
    // Search in asks
    for (auto& entry : asks_) {
        double price = entry.first;
        auto& orders = entry.second;
        for (auto it = orders.begin(); it != orders.end(); ++it) {
            if (it->order_id == order_id) {
                orders.erase(it);
                if (orders.empty()) {
                    asks_.erase(price);
                }
                return true;
            }
        }
    }
    
    return false;
}

// Match an incoming order against existing orders
std::vector<Trade> OrderBook::matchOrder(Order& incoming) {
    std::vector<Trade> trades;
    
    static uint64_t trade_id = 1;
    
    if (incoming.side == OrderSide::BUY) {
        while (incoming.remaining_quantity > 0 && !asks_.empty()) {
            auto bestAskIt = asks_.begin();
            double bestAskPrice = bestAskIt->first;
            
            if (incoming.type != OrderType::MARKET && 
                incoming.price < bestAskPrice) {
                break;
            }
            
            auto& askOrders = bestAskIt->second;
            Order& sellOrder = askOrders.front();
            
            uint32_t tradeQty = std::min(incoming.remaining_quantity, 
                                         sellOrder.remaining_quantity);
            
            incoming.remaining_quantity -= tradeQty;
            sellOrder.remaining_quantity -= tradeQty;
            
            trades.emplace_back(trade_id++, incoming.order_id, 
                                sellOrder.order_id, bestAskPrice, tradeQty);
            
            if (sellOrder.remaining_quantity == 0) {
                askOrders.erase(askOrders.begin());
                if (askOrders.empty()) {
                    asks_.erase(bestAskIt);
                }
            }
        }
    } else {
        while (incoming.remaining_quantity > 0 && !bids_.empty()) {
            auto bestBidIt = bids_.begin();
            double bestBidPrice = bestBidIt->first;
            
            if (incoming.type != OrderType::MARKET && 
                incoming.price > bestBidPrice) {
                break;
            }
            
            auto& bidOrders = bestBidIt->second;
            Order& buyOrder = bidOrders.front();
            
            uint32_t tradeQty = std::min(incoming.remaining_quantity, 
                                         buyOrder.remaining_quantity);
            
            incoming.remaining_quantity -= tradeQty;
            buyOrder.remaining_quantity -= tradeQty;
            
            trades.emplace_back(trade_id++, buyOrder.order_id, 
                                incoming.order_id, bestBidPrice, tradeQty);
            
            if (buyOrder.remaining_quantity == 0) {
                bidOrders.erase(bidOrders.begin());
                if (bidOrders.empty()) {
                    bids_.erase(bestBidIt);
                }
            }
        }
    }
    
    if (incoming.remaining_quantity > 0 && 
        incoming.type != OrderType::MARKET && 
        incoming.type != OrderType::IOC && 
        incoming.type != OrderType::FOK) {
        addOrder(incoming);
    }
    
    return trades;
}

double OrderBook::getBestBid() const {
    if (bids_.empty()) return 0;
    return bids_.begin()->first;
}

double OrderBook::getBestAsk() const {
    if (asks_.empty()) return 0;
    return asks_.begin()->first;
}

size_t OrderBook::getOrderCount() const {
    size_t count = 0;
    for (const auto& entry : bids_) {
        count += entry.second.size();
    }
    for (const auto& entry : asks_) {
        count += entry.second.size();
    }
    return count;
}

void OrderBook::printBook() const {
    std::cout << "\n=== ORDER BOOK ===\n";
    std::cout << "BIDS (Buy Orders):\n";
    for (const auto& entry : bids_) {
        const auto& orders = entry.second;
        for (const auto& order : orders) {
            std::cout << "  " << order.to_string() << "\n";
        }
    }
    std::cout << "ASKS (Sell Orders):\n";
    for (const auto& entry : asks_) {
        const auto& orders = entry.second;
        for (const auto& order : orders) {
            std::cout << "  " << order.to_string() << "\n";
        }
    }
    std::cout << "==================\n\n";
}