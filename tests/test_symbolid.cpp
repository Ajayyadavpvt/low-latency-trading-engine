// tests/test_symbolid.cpp
#include <gtest/gtest.h>
#include "SymbolId.h"
#include <limits>

TEST(SymbolTableTest, RegisterAndLookup) {
    SymbolTable table(4);
    SymbolId btc = table.registerSymbol("BTCUSD");
    SymbolId eth = table.registerSymbol("ETHUSD");
    SymbolId aapl = table.registerSymbol("AAPL");

    EXPECT_EQ(btc, 0);
    EXPECT_EQ(eth, 1);
    EXPECT_EQ(aapl, 2);

    EXPECT_EQ(table.getSymbol(btc), "BTCUSD");
    EXPECT_EQ(table.getSymbol(eth), "ETHUSD");

    SymbolId btc_again = table.registerSymbol("BTCUSD");
    EXPECT_EQ(btc_again, btc);
}

TEST(SymbolTableTest, ShardAssignment) {
    SymbolTable table(4);
    SymbolId sym0 = table.registerSymbol("SYM0");
    SymbolId sym1 = table.registerSymbol("SYM1");
    SymbolId sym2 = table.registerSymbol("SYM2");
    SymbolId sym3 = table.registerSymbol("SYM3");
    SymbolId sym4 = table.registerSymbol("SYM4");

    EXPECT_EQ(table.getShardIndex(sym0), 0);
    EXPECT_EQ(table.getShardIndex(sym1), 1);
    EXPECT_EQ(table.getShardIndex(sym2), 2);
    EXPECT_EQ(table.getShardIndex(sym3), 3);
    EXPECT_EQ(table.getShardIndex(sym4), 0);
}

TEST(SymbolTableTest, InvalidIdReturnsSentinel) {
    SymbolTable table(4);
    EXPECT_EQ(table.getSymbol(999), "UNKNOWN");
    EXPECT_EQ(table.getShardIndex(999), static_cast<size_t>(-1));
}

TEST(SymbolTableTest, NumShardsZeroThrows) {
    EXPECT_THROW(SymbolTable(0), std::invalid_argument);
}

TEST(SymbolTableTest, RegisterAfterFreezeThrows) {
    SymbolTable table(4);
    table.registerSymbol("BTCUSD");
    table.freeze();
    EXPECT_THROW(table.registerSymbol("ETHUSD"), std::runtime_error);
}