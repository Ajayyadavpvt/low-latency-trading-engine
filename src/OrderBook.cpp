// src/OrderBook.cpp
#include "../include/OrderBook.h"
#include <iostream>
#include <algorithm>

OrderBook::OrderBook() : next_trade_id_(1), stp_policy_(STPPolicy::NONE) {}

size_t OrderBook::findBidLevel(double price) const {
    size_t low = 0, high = bids_.size();
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (bids_[mid].price > price) low = mid + 1;
        else high = mid;
    }
    return low;
}

size_t OrderBook::findAskLevel(double price) const {
    size_t low = 0, high = asks_.size();
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (asks_[mid].price < price) low = mid + 1;
        else high = mid;
    }
    return low;
}

void OrderBook::addOrder(const Order& order) {
    if (order.side == OrderSide::BUY) {
        size_t idx = findBidLevel(order.price);
        if (idx < bids_.size() && bids_[idx].price == order.price) {
            bids_[idx].orders.push_back(order);
        } else {
            PriceLevel level;
            level.price = order.price;
            level.orders.push_back(order);
            bids_.insert(bids_.begin() + idx, std::move(level));
        }
    } else {
        size_t idx = findAskLevel(order.price);
        if (idx < asks_.size() && asks_[idx].price == order.price) {
            asks_[idx].orders.push_back(order);
        } else {
            PriceLevel level;
            level.price = order.price;
            level.orders.push_back(order);
            asks_.insert(asks_.begin() + idx, std::move(level));
        }
    }
}

bool OrderBook::cancelOrder(uint64_t order_id) {
    for (size_t i = 0; i < bids_.size(); ++i) {
        auto& level = bids_[i];
        for (auto it = level.orders.begin(); it != level.orders.end(); ++it) {
            if (it->order_id == order_id) {
                level.orders.erase(it);
                if (level.orders.empty()) bids_.erase(bids_.begin() + i);
                return true;
            }
        }
    }
    for (size_t i = 0; i < asks_.size(); ++i) {
        auto& level = asks_[i];
        for (auto it = level.orders.begin(); it != level.orders.end(); ++it) {
            if (it->order_id == order_id) {
                level.orders.erase(it);
                if (level.orders.empty()) asks_.erase(asks_.begin() + i);
                return true;
            }
        }
    }
    return false;
}

bool OrderBook::canFullyFill(const Order& incoming) const {
    uint32_t needed = incoming.remaining_quantity;
    uint32_t available = 0;

    if (incoming.side == OrderSide::BUY) {
        for (const auto& level : asks_) {
            if (incoming.type != OrderType::MARKET && incoming.price < level.price) break;
            for (const auto& order : level.orders) {
                available += order.remaining_quantity;
                if (available >= needed) return true;
            }
        }
    } else {
        for (const auto& level : bids_) {
            if (incoming.type != OrderType::MARKET && incoming.price > level.price) break;
            for (const auto& order : level.orders) {
                available += order.remaining_quantity;
                if (available >= needed) return true;
            }
        }
    }
    return false;
}

bool OrderBook::wouldSelfTrade(const Order& incoming) const {
    uint32_t remaining = incoming.remaining_quantity;
    uint64_t trader = incoming.trader_id;

    if (incoming.side == OrderSide::BUY) {
        for (const auto& level : asks_) {
            if (incoming.type != OrderType::MARKET && incoming.price < level.price) break;
            for (const auto& order : level.orders) {
                if (order.remaining_quantity == 0) continue;
                if (order.trader_id == trader) return true;
                uint32_t consume = std::min(remaining, order.remaining_quantity);
                remaining -= consume;
                if (remaining == 0) return false;
            }
        }
    } else {
        for (const auto& level : bids_) {
            if (incoming.type != OrderType::MARKET && incoming.price > level.price) break;
            for (const auto& order : level.orders) {
                if (order.remaining_quantity == 0) continue;
                if (order.trader_id == trader) return true;
                uint32_t consume = std::min(remaining, order.remaining_quantity);
                remaining -= consume;
                if (remaining == 0) return false;
            }
        }
    }
    return false;
}

std::vector<Trade> OrderBook::matchOrder(Order& incoming) {
    std::vector<Trade> trades;

    // STP handling first
    if (stp_policy_ != STPPolicy::NONE && wouldSelfTrade(incoming)) {
        switch (stp_policy_) {
            case STPPolicy::CANCEL_NEWEST:
                return trades;

            case STPPolicy::CANCEL_OLDEST:
            case STPPolicy::CANCEL_BOTH: {
                if (incoming.side == OrderSide::BUY) {
                    for (size_t i = 0; i < asks_.size(); ) {
                        if (incoming.type != OrderType::MARKET && incoming.price < asks_[i].price) break;
                        auto& level = asks_[i];
                        bool removed = false;
                        for (auto it = level.orders.begin(); it != level.orders.end(); ) {
                            if (it->trader_id == incoming.trader_id && it->remaining_quantity > 0) {
                                it = level.orders.erase(it);
                                removed = true;
                                if (stp_policy_ == STPPolicy::CANCEL_OLDEST) break;
                            } else {
                                ++it;
                            }
                        }
                        if (level.orders.empty()) asks_.erase(asks_.begin() + i);
                        else ++i;
                        if (stp_policy_ == STPPolicy::CANCEL_OLDEST && removed) break;
                    }
                } else {
                    for (size_t i = 0; i < bids_.size(); ) {
                        if (incoming.type != OrderType::MARKET && incoming.price > bids_[i].price) break;
                        auto& level = bids_[i];
                        bool removed = false;
                        for (auto it = level.orders.begin(); it != level.orders.end(); ) {
                            if (it->trader_id == incoming.trader_id && it->remaining_quantity > 0) {
                                it = level.orders.erase(it);
                                removed = true;
                                if (stp_policy_ == STPPolicy::CANCEL_OLDEST) break;
                            } else {
                                ++it;
                            }
                        }
                        if (level.orders.empty()) bids_.erase(bids_.begin() + i);
                        else ++i;
                        if (stp_policy_ == STPPolicy::CANCEL_OLDEST && removed) break;
                    }
                }

                if (stp_policy_ == STPPolicy::CANCEL_BOTH) return trades;
                break;
            }

            default:
                break;
        }
    }

    // FOK check AFTER STP
    if (incoming.type == OrderType::FOK) {
        if (!canFullyFill(incoming)) return trades;
    }

    // Matching loop
    if (incoming.side == OrderSide::BUY) {
        while (incoming.remaining_quantity > 0 && !asks_.empty()) {
            PriceLevel& level = asks_.front();
            if (incoming.type != OrderType::MARKET && incoming.price < level.price) break;

            // Skip cancelled orders (remaining_quantity == 0)
            while (!level.orders.empty() && level.orders.front().remaining_quantity == 0) {
                level.orders.pop_front();
            }
            if (level.orders.empty()) {
                asks_.erase(asks_.begin());
                continue;
            }

            Order& sellOrder = level.orders.front();
            uint32_t tradeQty = std::min(incoming.remaining_quantity, sellOrder.remaining_quantity);
            incoming.remaining_quantity -= tradeQty;
            sellOrder.remaining_quantity -= tradeQty;

            trades.emplace_back(next_trade_id_++, incoming.order_id, sellOrder.order_id,
                                incoming.trader_id, sellOrder.trader_id,
                                level.price, tradeQty);

            if (sellOrder.remaining_quantity == 0) {
                level.orders.pop_front();
                if (level.orders.empty()) asks_.erase(asks_.begin());
            }
        }
    } else {
        while (incoming.remaining_quantity > 0 && !bids_.empty()) {
            PriceLevel& level = bids_.front();
            if (incoming.type != OrderType::MARKET && incoming.price > level.price) break;

            while (!level.orders.empty() && level.orders.front().remaining_quantity == 0) {
                level.orders.pop_front();
            }
            if (level.orders.empty()) {
                bids_.erase(bids_.begin());
                continue;
            }

            Order& buyOrder = level.orders.front();
            uint32_t tradeQty = std::min(incoming.remaining_quantity, buyOrder.remaining_quantity);
            incoming.remaining_quantity -= tradeQty;
            buyOrder.remaining_quantity -= tradeQty;

            trades.emplace_back(next_trade_id_++, buyOrder.order_id, incoming.order_id,
                                buyOrder.trader_id, incoming.trader_id,
                                level.price, tradeQty);

            if (buyOrder.remaining_quantity == 0) {
                level.orders.pop_front();
                if (level.orders.empty()) bids_.erase(bids_.begin());
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
    return bids_.front().price;
}

double OrderBook::getBestAsk() const {
    if (asks_.empty()) return 0;
    return asks_.front().price;
}

size_t OrderBook::getOrderCount() const {
    size_t count = 0;
    for (const auto& level : bids_) count += level.orders.size();
    for (const auto& level : asks_) count += level.orders.size();
    return count;
}

void OrderBook::printBook() const {
    std::cout << "\n=== ORDER BOOK ===\n";
    std::cout << "BIDS (Buy Orders):\n";
    for (const auto& level : bids_)
        for (const auto& order : level.orders)
            if (order.remaining_quantity > 0)
                std::cout << "  " << order.to_string() << "\n";
    std::cout << "ASKS (Sell Orders):\n";
    for (const auto& level : asks_)
        for (const auto& order : level.orders)
            if (order.remaining_quantity > 0)
                std::cout << "  " << order.to_string() << "\n";
    std::cout << "==================\n\n";
}