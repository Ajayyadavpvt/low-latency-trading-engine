// src/main.cpp
#include "../include/MatchingEngine.h"
#include <iostream>
#include <vector>

int main() {
    MatchingEngine engine;

    std::cout << "=== Low Latency Trading Engine - Basic Test ===\n\n";

    // 1. Add a buy order (LIMIT) 100 shares @ 100.50
    Order buy1(1, 1001, OrderSide::BUY, OrderType::LIMIT, 100.50, 100);
    auto trades = engine.processOrder(buy1);
    std::cout << "After adding BUY 100 @ 100.50:\n";
    engine.printBook();
    std::cout << "Trades: " << trades.size() << "\n\n";

    // 2. Add a sell order (LIMIT) 50 shares @ 100.50 (fully match)
    Order sell1(2, 2001, OrderSide::SELL, OrderType::LIMIT, 100.50, 50);
    trades = engine.processOrder(sell1);
    std::cout << "After adding SELL 50 @ 100.50:\n";
    engine.printBook();
    std::cout << "Trades executed:\n";
    for (auto& t : trades) {
        std::cout << "  " << t.to_string() << "\n";
    }
    std::cout << "\n";

    // 3. Add another sell order (LIMIT) 60 shares @ 100.50 
    // (match remaining 50 of buy1, then 10 shares remain in ask book)
    Order sell2(3, 2002, OrderSide::SELL, OrderType::LIMIT, 100.50, 60);
    trades = engine.processOrder(sell2);
    std::cout << "After adding SELL 60 @ 100.50:\n";
    engine.printBook();
    std::cout << "Trades executed:\n";
    for (auto& t : trades) {
        std::cout << "  " << t.to_string() << "\n";
    }
    std::cout << "\n";

    // 4. Show best bid/ask
    std::cout << "Best Bid: " << engine.getBestBid() << "\n";
    std::cout << "Best Ask: " << engine.getBestAsk() << "\n";
    std::cout << "Total orders in book: " << engine.getOrderCount() << "\n\n";

    // 5. Test cancel order: cancel the remaining sell order (order_id=3)
    bool cancelled = engine.cancelOrder(3);
    std::cout << "Cancel order 3: " << (cancelled ? "Success" : "Failed") << "\n";
    engine.printBook();

    // 6. Test market order (will match against best ask)
    // First add a new ask so market order has something to match
    Order sell3(4, 2003, OrderSide::SELL, OrderType::LIMIT, 101.00, 30);
    engine.processOrder(sell3);
    Order buyMarket(5, 1005, OrderSide::BUY, OrderType::MARKET, 0, 20);
    trades = engine.processOrder(buyMarket);
    std::cout << "Market BUY 20:\n";
    for (auto& t : trades) {
        std::cout << "  " << t.to_string() << "\n";
    }
    engine.printBook();

    return 0;
}