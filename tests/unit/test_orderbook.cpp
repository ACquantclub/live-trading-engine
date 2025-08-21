#include "trading/core/order.hpp"
#include "trading/core/orderbook.hpp"
#include <memory>
#include <gtest/gtest.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace trading::core;

class OrderBookTest : public ::testing::Test {
  protected:
    void SetUp() override {
        orderbook_ = std::make_unique<OrderBook>("AAPL");
    }

    std::unique_ptr<OrderBook> orderbook_;
};

TEST_F(OrderBookTest, InitialState) {
    EXPECT_EQ(orderbook_->getSymbol(), "AAPL");
    EXPECT_EQ(orderbook_->getBestBid(), 0.0);
    EXPECT_EQ(orderbook_->getBestAsk(), 0.0);
    EXPECT_TRUE(orderbook_->getBuyOrders().empty());
    EXPECT_TRUE(orderbook_->getSellOrders().empty());
}

TEST_F(OrderBookTest, AddSingleBuyOrder) {
    auto order =
        std::make_shared<Order>("1", "user1", "AAPL", OrderType::LIMIT, OrderSide::BUY, 100, 150.0);
    ASSERT_TRUE(orderbook_->addOrder(order));

    EXPECT_EQ(orderbook_->getBestBid(), 150.0);
    EXPECT_EQ(orderbook_->getBestAsk(), 0.0);
    ASSERT_EQ(orderbook_->getBuyOrders().size(), 1);
    EXPECT_EQ(orderbook_->getBuyOrders()[0]->getId(), "1");
    EXPECT_TRUE(orderbook_->getSellOrders().empty());
}

TEST_F(OrderBookTest, AddSingleSellOrder) {
    auto order = std::make_shared<Order>("2", "user1", "AAPL", OrderType::LIMIT, OrderSide::SELL,
                                         100, 151.0);
    ASSERT_TRUE(orderbook_->addOrder(order));

    EXPECT_EQ(orderbook_->getBestBid(), 0.0);
    EXPECT_EQ(orderbook_->getBestAsk(), 151.0);
    ASSERT_EQ(orderbook_->getSellOrders().size(), 1);
    EXPECT_EQ(orderbook_->getSellOrders()[0]->getId(), "2");
    EXPECT_TRUE(orderbook_->getBuyOrders().empty());
}

TEST_F(OrderBookTest, AddMultipleOrders) {
    auto buy_order1 =
        std::make_shared<Order>("1", "user1", "AAPL", OrderType::LIMIT, OrderSide::BUY, 100, 150.0);
    auto buy_order2 =
        std::make_shared<Order>("2", "user2", "AAPL", OrderType::LIMIT, OrderSide::BUY, 100, 150.5);
    auto sell_order1 = std::make_shared<Order>("3", "user3", "AAPL", OrderType::LIMIT,
                                               OrderSide::SELL, 100, 151.0);
    auto sell_order2 = std::make_shared<Order>("4", "user4", "AAPL", OrderType::LIMIT,
                                               OrderSide::SELL, 100, 150.8);

    orderbook_->addOrder(buy_order1);
    orderbook_->addOrder(buy_order2);
    orderbook_->addOrder(sell_order1);
    orderbook_->addOrder(sell_order2);

    EXPECT_EQ(orderbook_->getBestBid(), 150.5);
    EXPECT_EQ(orderbook_->getBestAsk(), 150.8);
    EXPECT_NEAR(orderbook_->getSpread(), 0.3, 1e-9);
    EXPECT_EQ(orderbook_->getBuyOrders().size(), 2);
    EXPECT_EQ(orderbook_->getSellOrders().size(), 2);
}

TEST_F(OrderBookTest, AddOrderWithWrongSymbol) {
    auto order = std::make_shared<Order>("1", "user1", "GOOG", OrderType::LIMIT, OrderSide::BUY,
                                         100, 2800.0);
    EXPECT_FALSE(orderbook_->addOrder(order));
}

TEST_F(OrderBookTest, AddNullOrder) {
    EXPECT_FALSE(orderbook_->addOrder(nullptr));
}

TEST_F(OrderBookTest, SpreadCalculation) {
    auto buy_order = std::make_shared<Order>("1", "user1", "AAPL", OrderType::LIMIT, OrderSide::BUY,
                                             100, 149.95);
    auto sell_order = std::make_shared<Order>("2", "user2", "AAPL", OrderType::LIMIT,
                                              OrderSide::SELL, 100, 150.05);

    orderbook_->addOrder(buy_order);
    orderbook_->addOrder(sell_order);

    EXPECT_EQ(orderbook_->getBestBid(), 149.95);
    EXPECT_EQ(orderbook_->getBestAsk(), 150.05);
    EXPECT_NEAR(orderbook_->getSpread(), 0.10, 1e-9);
}

TEST_F(OrderBookTest, GetOrdersReturnsCorrectlySorted) {
    auto buy_order1 =
        std::make_shared<Order>("1", "user1", "AAPL", OrderType::LIMIT, OrderSide::BUY, 100, 150.0);
    auto buy_order2 =
        std::make_shared<Order>("2", "user2", "AAPL", OrderType::LIMIT, OrderSide::BUY, 100, 150.5);
    auto sell_order1 = std::make_shared<Order>("3", "user3", "AAPL", OrderType::LIMIT,
                                               OrderSide::SELL, 100, 151.0);
    auto sell_order2 = std::make_shared<Order>("4", "user4", "AAPL", OrderType::LIMIT,
                                               OrderSide::SELL, 100, 150.8);

    orderbook_->addOrder(buy_order1);
    orderbook_->addOrder(buy_order2);
    orderbook_->addOrder(sell_order1);
    orderbook_->addOrder(sell_order2);

    auto buy_orders = orderbook_->getBuyOrders();
    ASSERT_EQ(buy_orders.size(), 2);
    EXPECT_EQ(buy_orders[0]->getPrice(), 150.5);  // Best bid first
    EXPECT_EQ(buy_orders[1]->getPrice(), 150.0);

    auto sell_orders = orderbook_->getSellOrders();
    ASSERT_EQ(sell_orders.size(), 2);
    EXPECT_EQ(sell_orders[0]->getPrice(), 150.8);  // Best ask first
    EXPECT_EQ(sell_orders[1]->getPrice(), 151.0);
}

TEST_F(OrderBookTest, ToJSONSerialization) {
    auto buy_order1 =
        std::make_shared<Order>("1", "user1", "AAPL", OrderType::LIMIT, OrderSide::BUY, 100, 150.0);
    auto buy_order2 =
        std::make_shared<Order>("2", "user2", "AAPL", OrderType::LIMIT, OrderSide::BUY, 50, 150.5);
    auto sell_order1 = std::make_shared<Order>("3", "user3", "AAPL", OrderType::LIMIT,
                                               OrderSide::SELL, 100, 151.0);
    auto sell_order2 =
        std::make_shared<Order>("4", "user4", "AAPL", OrderType::LIMIT, OrderSide::SELL, 75, 150.8);

    orderbook_->addOrder(buy_order1);
    orderbook_->addOrder(buy_order2);
    orderbook_->addOrder(sell_order1);
    orderbook_->addOrder(sell_order2);

    std::string json_output = orderbook_->toJSON();
    auto parsed_json = json::parse(json_output);

    EXPECT_EQ(parsed_json["symbol"], "AAPL");
    EXPECT_EQ(parsed_json["best_bid"], 150.5);
    EXPECT_EQ(parsed_json["best_ask"], 150.8);
    EXPECT_NEAR(parsed_json["spread"], 0.3, 1e-9);

    auto bids = parsed_json["bids"];
    ASSERT_EQ(bids.size(), 2);
    EXPECT_EQ(bids[0]["price"], 150.5);
    EXPECT_EQ(bids[0]["quantity"], 50);
    EXPECT_EQ(bids[1]["price"], 150.0);
    EXPECT_EQ(bids[1]["quantity"], 100);

    auto asks = parsed_json["asks"];
    ASSERT_EQ(asks.size(), 2);
    EXPECT_EQ(asks[0]["price"], 150.8);
    EXPECT_EQ(asks[0]["quantity"], 75);
    EXPECT_EQ(asks[1]["price"], 151.0);
    EXPECT_EQ(asks[1]["quantity"], 100);
}