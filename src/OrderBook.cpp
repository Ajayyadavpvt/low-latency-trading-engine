// src/OrderBook.cpp
#include "../include/OrderBook.h"
#include <iostream>
#include <algorithm>

OrderBook::OrderBook() : next_trade_id_(1), stp_policy_(STPPolicy::NONE) {}

void OrderBook::addOrder(const Order& order) {
    if (order.side == OrderSide::BUY) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }
}

bool OrderBook::cancelOrder(uint64_t order_id) {
    // Search bids
    for (auto it = bids_.begin(); it != bids_.end(); ++it) {
        auto& orders = it->second;
        for (auto oit = orders.begin(); oit != orders.end(); ++oit) {
            if (oit->order_id == order_id) {
                orders.erase(oit);
                if (orders.empty()) {
                    bids_.erase(it);
                }
                return true;
            }
        }
    }
    // Search asks
    for (auto it = asks_.begin(); it != asks_.end(); ++it) {
        auto& orders = it->second;
        for (auto oit = orders.begin(); oit != orders.end(); ++oit) {
            if (oit->order_id == order_id) {
                orders.erase(oit);
                if (orders.empty()) {
                    asks_.erase(it);
                }
                return true;
            }
        }
    }
    return false;
}

bool OrderBook::canFullyFill(const Order& incoming) const {
    uint32_t needed = incoming.remaining_quantity;
    uint32_t available = 0;
    uint64_t trader = incoming.trader_id;

    if (incoming.side == OrderSide::BUY) {
        for (const auto& entry : asks_) {
            double ask_price = entry.first;
            if (incoming.type != OrderType::MARKET && incoming.price < ask_price) {
                break;
            }
            for (const auto& order : entry.second) {
                // Exclude self-trades when STP policy is active
                if (stp_policy_ != STPPolicy::NONE && order.trader_id == trader) {
                    continue;
                }
                available += order.remaining_quantity;
                if (available >= needed) return true;
            }
        }
    } else {
        for (const auto& entry : bids_) {
            double bid_price = entry.first;
            if (incoming.type != OrderType::MARKET && incoming.price > bid_price) {
                break;
            }
            for (const auto& order : entry.second) {
                // Exclude self-trades when STP policy is active
                if (stp_policy_ != STPPolicy::NONE && order.trader_id == trader) {
                    continue;
                }
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
        for (const auto& entry : asks_) {
            double ask_price = entry.first;
            if (incoming.type != OrderType::MARKET && incoming.price < ask_price) {
                break;
            }
            for (const auto& order : entry.second) {
                // If we reach a same-trader order before our quantity is filled,
                // then a self-trade would actually occur.
                if (order.trader_id == trader && order.remaining_quantity > 0) {
                    return true;
                }
                // Otherwise, consume this order's liquidity and continue.
                uint32_t consume = std::min(remaining, order.remaining_quantity);
                remaining -= consume;
                if (remaining == 0) {
                    return false; // fully filled before reaching self
                }
            }
        }
    } else {
        for (const auto& entry : bids_) {
            double bid_price = entry.first;
            if (incoming.type != OrderType::MARKET && incoming.price > bid_price) {
                break;
            }
            for (const auto& order : entry.second) {
                if (order.trader_id == trader && order.remaining_quantity > 0) {
                    return true;
                }
                uint32_t consume = std::min(remaining, order.remaining_quantity);
                remaining -= consume;
                if (remaining == 0) {
                    return false;
                }
            }
        }
    }
    return false;
}

std::vector<Trade> OrderBook::matchOrder(Order& incoming) {
    std::vector<Trade> trades;

    // FOK pre-check: if cannot fully fill, return empty
    if (incoming.type == OrderType::FOK) {
        if (!canFullyFill(incoming)) {
            return trades;
        }
    }

    // STP check — only when a policy is set
    if (stp_policy_ != STPPolicy::NONE && wouldSelfTrade(incoming)) {
        switch (stp_policy_) {
            case STPPolicy::CANCEL_NEWEST:
                return trades;

            case STPPolicy::CANCEL_OLDEST:
            case STPPolicy::CANCEL_BOTH: {
                bool cancelled = false;
                if (incoming.side == OrderSide::BUY) {
                    for (auto it = asks_.begin(); it != asks_.end() && !cancelled; ) {
                        double ask_price = it->first;
                        if (incoming.type != OrderType::MARKET && incoming.price < ask_price) {
                            break;
                        }
                        auto& orders = it->second;
                        for (auto oit = orders.begin(); oit != orders.end(); ++oit) {
                            if (oit->trader_id == incoming.trader_id && oit->remaining_quantity > 0) {
                                orders.erase(oit);
                                cancelled = true;
                                break;
                            }
                        }
                        if (orders.empty()) {
                            it = asks_.erase(it);
                        } else {
                            ++it;
                        }
                    }
                } else {
                    for (auto it = bids_.begin(); it != bids_.end() && !cancelled; ) {
                        double bid_price = it->first;
                        if (incoming.type != OrderType::MARKET && incoming.price > bid_price) {
                            break;
                        }
                        auto& orders = it->second;
                        for (auto oit = orders.begin(); oit != orders.end(); ++oit) {
                            if (oit->trader_id == incoming.trader_id && oit->remaining_quantity > 0) {
                                orders.erase(oit);
                                cancelled = true;
                                break;
                            }
                        }
                        if (orders.empty()) {
                            it = bids_.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                if (stp_policy_ == STPPolicy::CANCEL_BOTH) {
                    return trades;
                }
                break;
            }

            default:
                break;
        }
    }

    // === Matching loop ===
    if (incoming.side == OrderSide::BUY) {
        while (incoming.remaining_quantity > 0 && !asks_.empty()) {
            auto bestAskIt = asks_.begin();
            double bestAskPrice = bestAskIt->first;

            if (incoming.type != OrderType::MARKET && incoming.price < bestAskPrice) {
                break;
            }

            auto& askOrders = bestAskIt->second;
            Order& sellOrder = askOrders.front();

            // Guard: only skip same-trader when STP policy is active
            if (stp_policy_ != STPPolicy::NONE && sellOrder.trader_id == incoming.trader_id) {
                askOrders.erase(askOrders.begin());
                if (askOrders.empty()) {
                    asks_.erase(bestAskIt);
                }
                continue;
            }

            uint32_t tradeQty = std::min(incoming.remaining_quantity, sellOrder.remaining_quantity);
            incoming.remaining_quantity -= tradeQty;
            sellOrder.remaining_quantity -= tradeQty;

            trades.emplace_back(next_trade_id_++, incoming.order_id, sellOrder.order_id,
                                incoming.trader_id, sellOrder.trader_id,
                                bestAskPrice, tradeQty);

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

            if (incoming.type != OrderType::MARKET && incoming.price > bestBidPrice) {
                break;
            }

            auto& bidOrders = bestBidIt->second;
            Order& buyOrder = bidOrders.front();

            if (stp_policy_ != STPPolicy::NONE && buyOrder.trader_id == incoming.trader_id) {
                bidOrders.erase(bidOrders.begin());
                if (bidOrders.empty()) {
                    bids_.erase(bestBidIt);
                }
                continue;
            }

            uint32_t tradeQty = std::min(incoming.remaining_quantity, buyOrder.remaining_quantity);
            incoming.remaining_quantity -= tradeQty;
            buyOrder.remaining_quantity -= tradeQty;

            trades.emplace_back(next_trade_id_++, buyOrder.order_id, incoming.order_id,
                                buyOrder.trader_id, incoming.trader_id,
                                bestBidPrice, tradeQty);

            if (buyOrder.remaining_quantity == 0) {
                bidOrders.erase(bidOrders.begin());
                if (bidOrders.empty()) {
                    bids_.erase(bestBidIt);
                }
            }
        }
    }

    // Add remaining to book if applicable
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
    for (const auto& entry : bids_) count += entry.second.size();
    for (const auto& entry : asks_) count += entry.second.size();
    return count;
}

void OrderBook::printBook() const {
    std::cout << "\n=== ORDER BOOK ===\n";
    std::cout << "BIDS (Buy Orders):\n";
    for (const auto& entry : bids_)
        for (const auto& order : entry.second)
            std::cout << "  " << order.to_string() << "\n";
    std::cout << "ASKS (Sell Orders):\n";
    for (const auto& entry : asks_)
        for (const auto& order : entry.second)
            std::cout << "  " << order.to_string() << "\n";
    std::cout << "==================\n\n";
}