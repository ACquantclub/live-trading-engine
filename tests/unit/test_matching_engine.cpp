#include "trading/core/matching_engine.hpp"
#include "trading/core/order.hpp"
#include "trading/core/orderbook.hpp"
#include "trading/core/user.hpp"
#include <gtest/gtest.h>

using namespace trading::core;

class MatchingEngineTest : public ::testing::Test {
  protected:
    void SetUp() override {
        matching_engine_ = std::make_unique<MatchingEngine>();
        orderbook_ = std::make_shared<OrderBook>("AAPL");
        matching_engine_->addOrderBook("AAPL", orderbook_);

        // Create users with starting cash
        buyer_ = std::make_shared<User>("user-001", 10000.0);
        seller_ = std::make_shared<User>("user-002", 10000.0);

        // Add users to matching engine
        matching_engine_->addUser(buyer_);
        matching_engine_->addUser(seller_);

        // Give seller some initial position to sell
        seller_->applyExecution(OrderSide::BUY, "AAPL", 100.0, 50.0, 0.0);
    }

    std::unique_ptr<MatchingEngine> matching_engine_;
    std::shared_ptr<OrderBook> orderbook_;
    std::shared_ptr<User> buyer_;
    std::shared_ptr<User> seller_;
};

// Basic functionality tests
TEST_F(MatchingEngineTest, InitialState) {
    EXPECT_EQ(matching_engine_->getTotalTrades(), 0);
    EXPECT_NEAR(matching_engine_->getTotalVolume(), 0.0, 1e-9);
    EXPECT_EQ(matching_engine_->getOrderBook("AAPL"), orderbook_);
    EXPECT_EQ(matching_engine_->getOrderBook("MSFT"), nullptr);
}

TEST_F(MatchingEngineTest, AddOrderBook) {
    auto new_orderbook = std::make_shared<OrderBook>("MSFT");
    matching_engine_->addOrderBook("MSFT", new_orderbook);
    EXPECT_EQ(matching_engine_->getOrderBook("MSFT"), new_orderbook);
}

TEST_F(MatchingEngineTest, LimitOrderMatchingSamePriceBuyFirst) {
    // Add buy order first
    auto buy_order = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::LIMIT,
                                             OrderSide::BUY, 100.0, 50.0);
    orderbook_->addOrder(buy_order);

    // Add matching sell order
    auto sell_order = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::LIMIT,
                                              OrderSide::SELL, 100.0, 50.0);
    auto trades = matching_engine_->matchOrder(sell_order, *orderbook_);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 100.0);
    EXPECT_EQ(trades[0].price, 50.0);  // Should match at sell order's price
    EXPECT_EQ(trades[0].buy_user_id, "user-001");
    EXPECT_EQ(trades[0].sell_user_id, "user-002");

    EXPECT_EQ(matching_engine_->getTotalTrades(), 1);
    EXPECT_NEAR(matching_engine_->getTotalVolume(), 5000.0, 1e-9);
}

TEST_F(MatchingEngineTest, LimitOrderMatchingSamePriceSellFirst) {
    // Add sell order first
    auto sell_order = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::LIMIT,
                                              OrderSide::SELL, 100.0, 50.0);
    orderbook_->addOrder(sell_order);

    // Add matching buy order
    auto buy_order = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::LIMIT,
                                             OrderSide::BUY, 100.0, 50.0);
    auto trades = matching_engine_->matchOrder(buy_order, *orderbook_);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 100.0);
    EXPECT_EQ(trades[0].price, 50.0);  // Should match at buy order's price
    EXPECT_EQ(trades[0].buy_user_id, "user-001");
    EXPECT_EQ(trades[0].sell_user_id, "user-002");
}

TEST_F(MatchingEngineTest, LimitOrderPartialMatch) {
    // Add large sell order
    auto sell_order = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::LIMIT,
                                              OrderSide::SELL, 200.0, 50.0);
    orderbook_->addOrder(sell_order);

    // Add smaller buy order
    auto buy_order = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::LIMIT,
                                             OrderSide::BUY, 75.0, 50.0);
    auto trades = matching_engine_->matchOrder(buy_order, *orderbook_);

    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 75.0);
    EXPECT_EQ(trades[0].price, 50.0);

    // Verify remaining quantity in sell order
    EXPECT_NEAR(sell_order->getQuantity(), 125.0, 1e-9);
}

TEST_F(MatchingEngineTest, LimitOrderNoMatchWrongPrice) {
    // Add high-priced sell order
    auto sell_order = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::LIMIT,
                                              OrderSide::SELL, 100.0, 60.0);
    orderbook_->addOrder(sell_order);

    // Add low-priced buy order (no match should occur)
    auto buy_order = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::LIMIT,
                                             OrderSide::BUY, 100.0, 50.0);
    auto trades = matching_engine_->matchOrder(buy_order, *orderbook_);

    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(matching_engine_->getTotalTrades(), 0);
    EXPECT_NEAR(matching_engine_->getTotalVolume(), 0.0, 1e-9);
}

TEST_F(MatchingEngineTest, LimitOrderMultipleMatches) {
    // Add multiple sell orders
    auto sell_order1 = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::LIMIT,
                                               OrderSide::SELL, 50.0, 49.0);
    auto sell_order2 = std::make_shared<Order>("sell-2", "user-003", "AAPL", OrderType::LIMIT,
                                               OrderSide::SELL, 75.0, 50.0);
    orderbook_->addOrder(sell_order1);
    orderbook_->addOrder(sell_order2);

    // Add large buy order that should match both
    auto buy_order = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::LIMIT,
                                             OrderSide::BUY, 150.0, 50.0);
    auto trades = matching_engine_->matchOrder(buy_order, *orderbook_);

    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(matching_engine_->getTotalTrades(), 2);
}

TEST_F(MatchingEngineTest, MarketOrderMatchingBuy) {
    // Add sell orders with different prices
    auto sell_order1 = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::LIMIT,
                                               OrderSide::SELL, 50.0, 49.0);
    auto sell_order2 = std::make_shared<Order>("sell-2", "user-003", "AAPL", OrderType::LIMIT,
                                               OrderSide::SELL, 75.0, 51.0);
    orderbook_->addOrder(sell_order1);
    orderbook_->addOrder(sell_order2);

    // Market buy order should match at best ask (49.0)
    auto market_buy = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::MARKET,
                                              OrderSide::BUY, 100.0);
    auto trades = matching_engine_->matchOrder(market_buy, *orderbook_);

    EXPECT_GT(trades.size(), 0);
    // Should match with the best ask price
    if (!trades.empty()) {
        EXPECT_EQ(trades[0].price, 49.0);
    }
}

TEST_F(MatchingEngineTest, MarketOrderMatchingSell) {
    // Add buy orders with different prices
    auto buy_order1 = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::LIMIT,
                                              OrderSide::BUY, 50.0, 52.0);
    auto buy_order2 = std::make_shared<Order>("buy-2", "user-003", "AAPL", OrderType::LIMIT,
                                              OrderSide::BUY, 75.0, 50.0);
    orderbook_->addOrder(buy_order1);
    orderbook_->addOrder(buy_order2);

    // Market sell order should match at best bid (52.0)
    auto market_sell = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::MARKET,
                                               OrderSide::SELL, 100.0);
    auto trades = matching_engine_->matchOrder(market_sell, *orderbook_);

    EXPECT_GT(trades.size(), 0);
    // Should match with the best bid price
    if (!trades.empty()) {
        EXPECT_EQ(trades[0].price, 52.0);
    }
}

TEST_F(MatchingEngineTest, TradeCallbackFunctionality) {
    bool callback_called = false;
    std::vector<Trade> received_trades;

    matching_engine_->setTradeCallback([&](const Trade& trade) {
        callback_called = true;
        received_trades.push_back(trade);
    });

    // Create matching orders
    auto sell_order = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::LIMIT,
                                              OrderSide::SELL, 100.0, 50.0);
    orderbook_->addOrder(sell_order);

    auto buy_order = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::LIMIT,
                                             OrderSide::BUY, 100.0, 50.0);
    auto trades = matching_engine_->matchOrder(buy_order, *orderbook_);

    EXPECT_TRUE(callback_called);
    EXPECT_EQ(received_trades.size(), 1);
    EXPECT_EQ(received_trades[0].symbol, "AAPL");
    EXPECT_EQ(received_trades[0].quantity, 100.0);
    EXPECT_EQ(received_trades[0].price, 50.0);
}

// User portfolio update tests

TEST_F(MatchingEngineTest, MatchingUpdatesUserPortfolios) {
    // Initial state
    EXPECT_NEAR(buyer_->getCashBalance(), 10000.0, 1e-9);
    EXPECT_NEAR(seller_->getCashBalance(), 5000.0, 1e-9);  // 10000 - (100 * 50)

    auto seller_position = seller_->getPosition("AAPL");
    ASSERT_TRUE(seller_position.has_value());
    EXPECT_NEAR(seller_position->quantity, 100.0, 1e-9);
    EXPECT_NEAR(seller_position->average_price, 50.0, 1e-9);

    // Create orders
    auto sell_order = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::LIMIT,
                                              OrderSide::SELL, 50.0, 60.0);
    auto buy_order = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::LIMIT,
                                             OrderSide::BUY, 50.0, 60.0);

    // Add sell order first
    orderbook_->addOrder(sell_order);

    // Match buy order against sell order
    auto trades = matching_engine_->matchOrder(buy_order, *orderbook_);

    // Verify trade was created
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50.0);
    EXPECT_EQ(trades[0].price, 60.0);
    EXPECT_EQ(trades[0].buy_user_id, "user-001");
    EXPECT_EQ(trades[0].sell_user_id, "user-002");

    // Verify buyer's portfolio updated
    EXPECT_NEAR(buyer_->getCashBalance(), 10000.0 - (50.0 * 60.0), 1e-9);  // 10000 - 3000 = 7000
    auto buyer_position = buyer_->getPosition("AAPL");
    ASSERT_TRUE(buyer_position.has_value());
    EXPECT_NEAR(buyer_position->quantity, 50.0, 1e-9);
    EXPECT_NEAR(buyer_position->average_price, 60.0, 1e-9);

    // Verify seller's portfolio updated
    EXPECT_NEAR(seller_->getCashBalance(), 5000.0 + (50.0 * 60.0), 1e-9);  // 5000 + 3000 = 8000
    seller_position = seller_->getPosition("AAPL");
    ASSERT_TRUE(seller_position.has_value());
    EXPECT_NEAR(seller_position->quantity, 50.0, 1e-9);       // 100 - 50 = 50
    EXPECT_NEAR(seller_position->average_price, 50.0, 1e-9);  // unchanged

    // Verify seller's realized PnL
    double expected_pnl =
        (50.0 * 60.0) - (50.0 * 50.0);  // proceeds - cost basis = 3000 - 2500 = 500
    EXPECT_NEAR(seller_->getRealizedPnl(), expected_pnl, 1e-9);
}

TEST_F(MatchingEngineTest, TradeCallbackWithUserPortfolios) {
    bool callback_called = false;
    Trade received_trade;

    // Set up callback to capture trade
    matching_engine_->setTradeCallback([&](const Trade& trade) {
        callback_called = true;
        received_trade = trade;
    });

    // Create and match orders
    auto sell_order = std::make_shared<Order>("sell-1", "user-002", "AAPL", OrderType::LIMIT,
                                              OrderSide::SELL, 25.0, 55.0);
    auto buy_order = std::make_shared<Order>("buy-1", "user-001", "AAPL", OrderType::MARKET,
                                             OrderSide::BUY, 25.0);

    orderbook_->addOrder(sell_order);
    auto trades = matching_engine_->matchOrder(buy_order, *orderbook_);

    // Verify callback was called
    EXPECT_TRUE(callback_called);
    ASSERT_EQ(trades.size(), 1);

    // Verify trade data passed to callback
    EXPECT_EQ(received_trade.quantity, 25.0);
    EXPECT_EQ(received_trade.buy_user_id, "user-001");
    EXPECT_EQ(received_trade.sell_user_id, "user-002");
    EXPECT_EQ(received_trade.symbol, "AAPL");
}

TEST_F(MatchingEngineTest, UserRegistryFunctionality) {
    // Test getOrCreateUser for existing user
    auto existing_user = matching_engine_->getOrCreateUser("user-001");
    EXPECT_EQ(existing_user->getUserId(), "user-001");
    EXPECT_EQ(existing_user, buyer_);  // Should be the same instance

    // Test getOrCreateUser for new user
    auto new_user = matching_engine_->getOrCreateUser("user-003", 5000.0);
    EXPECT_EQ(new_user->getUserId(), "user-003");
    EXPECT_NEAR(new_user->getCashBalance(), 5000.0, 1e-9);

    // Test getUser for existing and non-existing users
    EXPECT_EQ(matching_engine_->getUser("user-001"), buyer_);
    EXPECT_EQ(matching_engine_->getUser("user-999"), nullptr);
}
