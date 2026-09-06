// src/OrderBook.cpp
#include "OrderBook.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>

OrderBook::OrderBook(size_t pool_capacity)
    : next_trade_id_(1)
    , stp_policy_(STPPolicy::NONE)
    , order_pool_(pool_capacity)
{
}

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
    OrderPool* pool = &order_pool_;
    if (order.side == OrderSide::BUY) {
        size_t idx = findBidLevel(order.price);
        if (idx < bids_.size() && bids_[idx].price == order.price) {
            bids_[idx].orders.push_back(order);
        } else {
            PriceLevel level(pool, order.price);
            level.orders.push_back(order);
            bids_.insert(bids_.begin() + idx, std::move(level));
        }
    } else {
        size_t idx = findAskLevel(order.price);
        if (idx < asks_.size() && asks_[idx].price == order.price) {
            asks_[idx].orders.push_back(order);
        } else {
            PriceLevel level(pool, order.price);
            level.orders.push_back(order);
            asks_.insert(asks_.begin() + idx, std::move(level));
        }
    }
}

bool OrderBook::cancelOrder(uint64_t order_id) {
    for (size_t i = 0; i < bids_.size(); ++i) {
        auto& level = bids_[i];
        for (size_t j = 0; j < level.orders.size(); ++j) {
            if (level.orders.at(j).order_id == order_id) {
                level.orders.erase_at(j);
                if (level.orders.empty()) {
                    bids_.erase(bids_.begin() + i);
                }
                return true;
            }
        }
    }
    for (size_t i = 0; i < asks_.size(); ++i) {
        auto& level = asks_[i];
        for (size_t j = 0; j < level.orders.size(); ++j) {
            if (level.orders.at(j).order_id == order_id) {
                level.orders.erase_at(j);
                if (level.orders.empty()) {
                    asks_.erase(asks_.begin() + i);
                }
                return true;
            }
        }
    }
    return false;
}

// Replace an existing order: cancel old, then submit new through matchOrder.
// If new_qty == 0, simply cancel the old order (success, no trades).
ReplaceResult OrderBook::replaceOrder(uint64_t order_id, double new_price, uint32_t new_qty) {
    // Validate price BEFORE touching the original order.
    if (new_qty > 0 && (!std::isfinite(new_price) || new_price <= 0.0)) {
        return ReplaceResult{false, {}};
    }

    // Search bids
    for (size_t i = 0; i < bids_.size(); ++i) {
        auto& level = bids_[i];
        for (size_t j = 0; j < level.orders.size(); ++j) {
            const Order& old = level.orders.at(j);
            if (old.order_id == order_id) {
                uint32_t already_filled = old.quantity - old.remaining_quantity;
                if (new_qty > 0 && new_qty < already_filled) {
                    return ReplaceResult{false, {}}; // cannot shrink below filled qty
                }

                // Capture fields before erasing
                uint64_t trader_id  = old.trader_id;
                OrderSide side      = old.side;
                OrderType type      = old.type;
                SymbolId  symbol_id = old.symbol_id;
                auto received_time  = old.received_time;

                level.orders.erase_at(j);
                if (level.orders.empty()) {
                    bids_.erase(bids_.begin() + i);
                }

                if (new_qty == 0) {
                    return ReplaceResult{true, {}}; // cancel only
                }

                Order new_order;
                new_order.order_id           = order_id;
                new_order.trader_id          = trader_id;
                new_order.side               = side;
                new_order.type               = type;
                new_order.price              = new_price;
                new_order.quantity           = new_qty;
                new_order.remaining_quantity = new_qty;
                new_order.symbol_id          = symbol_id;
                new_order.timestamp          = std::chrono::steady_clock::now().time_since_epoch();
                new_order.received_time      = received_time;

                auto trades = matchOrder(new_order);
                return ReplaceResult{true, std::move(trades)};
            }
        }
    }

    // Search asks
    for (size_t i = 0; i < asks_.size(); ++i) {
        auto& level = asks_[i];
        for (size_t j = 0; j < level.orders.size(); ++j) {
            const Order& old = level.orders.at(j);
            if (old.order_id == order_id) {
                uint32_t already_filled = old.quantity - old.remaining_quantity;
                if (new_qty > 0 && new_qty < already_filled) {
                    return ReplaceResult{false, {}};
                }

                uint64_t trader_id  = old.trader_id;
                OrderSide side      = old.side;
                OrderType type      = old.type;
                SymbolId  symbol_id = old.symbol_id;
                auto received_time  = old.received_time;

                level.orders.erase_at(j);
                if (level.orders.empty()) {
                    asks_.erase(asks_.begin() + i);
                }

                if (new_qty == 0) {
                    return ReplaceResult{true, {}};
                }

                Order new_order;
                new_order.order_id           = order_id;
                new_order.trader_id          = trader_id;
                new_order.side               = side;
                new_order.type               = type;
                new_order.price              = new_price;
                new_order.quantity           = new_qty;
                new_order.remaining_quantity = new_qty;
                new_order.symbol_id          = symbol_id;
                new_order.timestamp          = std::chrono::steady_clock::now().time_since_epoch();
                new_order.received_time      = received_time;

                auto trades = matchOrder(new_order);
                return ReplaceResult{true, std::move(trades)};
            }
        }
    }

    return ReplaceResult{false, {}}; // order not found
}

bool OrderBook::canFullyFill(const Order& incoming) const {
    uint64_t needed = incoming.remaining_quantity;
    uint64_t available = 0;

    if (incoming.side == OrderSide::BUY) {
        for (const auto& level : asks_) {
            if (incoming.type != OrderType::MARKET && incoming.price < level.price) break;
            for (size_t i = 0; i < level.orders.size(); ++i) {
                available += level.orders.at(i).remaining_quantity;
                if (available >= needed) return true;
            }
        }
    } else {
        for (const auto& level : bids_) {
            if (incoming.type != OrderType::MARKET && incoming.price > level.price) break;
            for (size_t i = 0; i < level.orders.size(); ++i) {
                available += level.orders.at(i).remaining_quantity;
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
            for (size_t i = 0; i < level.orders.size(); ++i) {
                const auto& order = level.orders.at(i);
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
            for (size_t i = 0; i < level.orders.size(); ++i) {
                const auto& order = level.orders.at(i);
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

    if (incoming.type == OrderType::FOK) {
        if (!canFullyFill(incoming)) return trades;
    }

    if (stp_policy_ != STPPolicy::NONE && wouldSelfTrade(incoming)) {
        switch (stp_policy_) {
            case STPPolicy::CANCEL_NEWEST:
                return trades;
            case STPPolicy::CANCEL_OLDEST:
            case STPPolicy::CANCEL_BOTH: {
                bool removed = false;
                if (incoming.side == OrderSide::BUY) {
                    for (size_t i = 0; i < asks_.size(); ) {
                        if (incoming.type != OrderType::MARKET && incoming.price < asks_[i].price) break;
                        auto& level = asks_[i];
                        for (size_t j = 0; j < level.orders.size(); ) {
                            if (level.orders.at(j).trader_id == incoming.trader_id &&
                                level.orders.at(j).remaining_quantity > 0) {
                                level.orders.erase_at(j);
                                removed = true;
                                if (stp_policy_ == STPPolicy::CANCEL_OLDEST) break;
                            } else {
                                ++j;
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
                        for (size_t j = 0; j < level.orders.size(); ) {
                            if (level.orders.at(j).trader_id == incoming.trader_id &&
                                level.orders.at(j).remaining_quantity > 0) {
                                level.orders.erase_at(j);
                                removed = true;
                                if (stp_policy_ == STPPolicy::CANCEL_OLDEST) break;
                            } else {
                                ++j;
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

    if (incoming.side == OrderSide::BUY) {
        while (incoming.remaining_quantity > 0 && !asks_.empty()) {
            PriceLevel& level = asks_.front();
            if (incoming.type != OrderType::MARKET && incoming.price < level.price) break;

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
        for (size_t i = 0; i < level.orders.size(); ++i)
            if (level.orders.at(i).remaining_quantity > 0)
                std::cout << "  " << level.orders.at(i).to_string() << "\n";
    std::cout << "ASKS (Sell Orders):\n";
    for (const auto& level : asks_)
        for (size_t i = 0; i < level.orders.size(); ++i)
            if (level.orders.at(i).remaining_quantity > 0)
                std::cout << "  " << level.orders.at(i).to_string() << "\n";
    std::cout << "==================\n\n";
}