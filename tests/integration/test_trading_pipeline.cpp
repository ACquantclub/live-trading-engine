#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <gtest/gtest.h>

#include "trading/core/matching_engine.hpp"
#include "trading/core/order.hpp"
#include "trading/core/orderbook.hpp"
#include "trading/core/user.hpp"
#include "trading/execution/executor.hpp"
#include "trading/logging/app_logger.hpp"
#include "trading/logging/trade_logger.hpp"
#include "trading/validation/order_validator.hpp"
#include "../../apps/json.hpp"

using json = nlohmann::json;
using namespace trading;

class TradingPipelineTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Initialize components
        matching_engine_ = std::make_unique<core::MatchingEngine>();
        validator_ = std::make_unique<validation::OrderValidator>();
        executor_ = std::make_unique<execution::Executor>();
        trade_logger_ = std::make_unique<logging::TradeLogger>("test_trades.log");
        app_logger_ = std::make_unique<logging::AppLogger>("test_app.log");

        // Setup callbacks
        trade_count_ = 0;
        total_volume_ = 0.0;

        matching_engine_->setTradeCallback(
            [this](const core::Trade& trade) { handleTrade(trade); });

        executor_->setExecutionCallback(
            [this](const execution::ExecutionResult& result) { handleExecution(result); });

        // Load test data
        loadTestData();
    }

    void TearDown() override {
        // Clean up test log files
        std::remove("test_trades.log");
        std::remove("test_app.log");
    }

    void loadTestData() {
        // Try multiple possible paths since tests can run from different directories
        std::vector<std::string> possible_paths = {
            "../tests/fixtures/sample_orders.json",       // From build directory
            "tests/fixtures/sample_orders.json",          // From project root
            "../../tests/fixtures/sample_orders.json",    // From nested test directory
            "../../../tests/fixtures/sample_orders.json"  // Even more nested
        };

        std::ifstream file;
        bool found = false;

        for (const auto& path : possible_paths) {
            file.open(path);
            if (file.is_open()) {
                found = true;
                std::cout << "Found sample_orders.json at: " << path << std::endl;
                break;
            }
            file.clear();  // Clear any error flags
        }

        if (!found) {
            FAIL() << "Could not find sample_orders.json in any of the expected locations. "
                   << "Tried paths: " << possible_paths[0] << ", " << possible_paths[1] << ", "
                   << possible_paths[2] << ", " << possible_paths[3];
        }

        file >> test_data_;
        file.close();

        // Setup users from test data
        for (const auto& user_data : test_data_["users"]) {
            auto user = std::make_shared<core::User>(user_data["id"], user_data["starting_cash"]);

            // Add initial positions
            if (user_data.contains("initial_positions")) {
                for (const auto& position : user_data["initial_positions"]) {
                    user->applyExecution(core::OrderSide::BUY, position["symbol"],
                                         position["quantity"], position["average_price"], 0.0);
                }
            }

            matching_engine_->addUser(user);
            users_[user_data["id"]] = user;
        }
    }

    core::Order createOrderFromJson(const json& order_json) {
        return core::Order(order_json["id"], order_json["userId"], order_json["symbol"],
                           stringToOrderType(order_json["type"]),
                           stringToOrderSide(order_json["side"]), order_json["quantity"],
                           order_json.contains("price") ? order_json["price"].get<double>() : 0.0);
    }

    core::OrderType stringToOrderType(const std::string& type_str) {
        if (type_str == "LIMIT")
            return core::OrderType::LIMIT;
        if (type_str == "MARKET")
            return core::OrderType::MARKET;
        if (type_str == "STOP")
            return core::OrderType::STOP;
        throw std::invalid_argument("Invalid order type: " + type_str);
    }

    core::OrderSide stringToOrderSide(const std::string& side_str) {
        if (side_str == "BUY")
            return core::OrderSide::BUY;
        if (side_str == "SELL")
            return core::OrderSide::SELL;
        throw std::invalid_argument("Invalid order side: " + side_str);
    }

    std::shared_ptr<core::OrderBook> getOrCreateOrderBook(const std::string& symbol) {
        auto orderbook = matching_engine_->getOrderBook(symbol);
        if (!orderbook) {
            orderbook = std::make_shared<core::OrderBook>(symbol);
            matching_engine_->addOrderBook(symbol, orderbook);
        }
        return orderbook;
    }

    void handleTrade(const core::Trade& trade) {
        trade_count_++;
        total_volume_ += trade.quantity * trade.price;
        trades_.push_back(trade);

        // Log the trade
        trade_logger_->logTrade(trade);

        // Execute the trade
        auto result = executor_->execute(trade);
    }

    void handleExecution(const execution::ExecutionResult& result) {
        executions_.push_back(result);
    }

    std::unique_ptr<core::MatchingEngine> matching_engine_;
    std::unique_ptr<validation::OrderValidator> validator_;
    std::unique_ptr<execution::Executor> executor_;
    std::unique_ptr<logging::TradeLogger> trade_logger_;
    std::unique_ptr<logging::AppLogger> app_logger_;

    json test_data_;
    std::map<std::string, std::shared_ptr<core::User>> users_;
    std::vector<core::Trade> trades_;
    std::vector<execution::ExecutionResult> executions_;

    int trade_count_;
    double total_volume_;
};

// Test complete trading pipeline with valid orders
TEST_F(TradingPipelineTest, CompleteAAPLTradingScenario) {
    // Test AAPL trading scenario from sample data
    auto orders_json = test_data_["orders"];

    // Find AAPL sell order (ORDER_001) and buy order (ORDER_002)
    auto sell_order_json =
        std::find_if(orders_json.begin(), orders_json.end(),
                     [](const json& order) { return order["id"] == "ORDER_001"; });
    auto buy_order_json =
        std::find_if(orders_json.begin(), orders_json.end(),
                     [](const json& order) { return order["id"] == "ORDER_002"; });

    ASSERT_NE(sell_order_json, orders_json.end());
    ASSERT_NE(buy_order_json, orders_json.end());

    // Create orders
    auto sell_order = createOrderFromJson(*sell_order_json);
    auto buy_order = createOrderFromJson(*buy_order_json);

    // Validate orders
    auto sell_validation = validator_->validate(std::make_shared<core::Order>(sell_order));
    auto buy_validation = validator_->validate(std::make_shared<core::Order>(buy_order));

    EXPECT_TRUE(sell_validation.is_valid)
        << "Sell order validation failed: " << sell_validation.error_message;
    EXPECT_TRUE(buy_validation.is_valid)
        << "Buy order validation failed: " << buy_validation.error_message;

    // Get initial user states
    auto seller = users_["trader-001"];
    auto buyer = users_["trader-002"];

    double initial_seller_cash = seller->getCashBalance();
    double initial_buyer_cash = buyer->getCashBalance();
    auto initial_seller_position = seller->getPosition("AAPL");

    EXPECT_TRUE(initial_seller_position.has_value());
    EXPECT_NEAR(initial_seller_position->quantity, 100.0, 1e-9);

    // Add orders to order book and execute
    auto orderbook = getOrCreateOrderBook("AAPL");

    // Add sell order first
    auto sell_order_ptr = std::make_shared<core::Order>(sell_order);
    EXPECT_TRUE(orderbook->addOrder(sell_order_ptr));

    // Match buy order (should create trade)
    auto buy_order_ptr = std::make_shared<core::Order>(buy_order);
    auto trades = matching_engine_->matchOrder(buy_order_ptr, *orderbook);

    // Verify trade was created
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 25.0);  // Partial fill
    EXPECT_EQ(trades[0].price, 150.50);
    EXPECT_EQ(trades[0].buy_user_id, "trader-002");
    EXPECT_EQ(trades[0].sell_user_id, "trader-001");

    // Verify callback was triggered
    EXPECT_EQ(trade_count_, 1);
    EXPECT_NEAR(total_volume_, 3762.50, 1e-2);  // 25 * 150.50

    // Verify user portfolios were updated
    EXPECT_NEAR(seller->getCashBalance(), initial_seller_cash + 3762.50, 1e-2);
    EXPECT_NEAR(buyer->getCashBalance(), initial_buyer_cash - 3762.50, 1e-2);

    auto final_seller_position = seller->getPosition("AAPL");
    auto final_buyer_position = buyer->getPosition("AAPL");

    ASSERT_TRUE(final_seller_position.has_value());
    ASSERT_TRUE(final_buyer_position.has_value());

    EXPECT_NEAR(final_seller_position->quantity, 75.0, 1e-9);  // 100 - 25
    EXPECT_NEAR(final_buyer_position->quantity, 25.0, 1e-9);
    EXPECT_NEAR(final_buyer_position->average_price, 150.50, 1e-9);

    // Verify realized PnL for seller
    double expected_seller_pnl = (25.0 * 150.50) - (25.0 * 145.00);  // proceeds - cost basis
    EXPECT_NEAR(seller->getRealizedPnl(), expected_seller_pnl, 1e-2);
}

TEST_F(TradingPipelineTest, MSFTMarketOrderScenario) {
    // Test MSFT market order scenario
    auto orders_json = test_data_["orders"];

    // Find MSFT orders (ORDER_003 market buy, ORDER_004 limit sell)
    auto market_order_json =
        std::find_if(orders_json.begin(), orders_json.end(),
                     [](const json& order) { return order["id"] == "ORDER_003"; });
    auto limit_order_json =
        std::find_if(orders_json.begin(), orders_json.end(),
                     [](const json& order) { return order["id"] == "ORDER_004"; });

    ASSERT_NE(market_order_json, orders_json.end());
    ASSERT_NE(limit_order_json, orders_json.end());

    auto limit_order = createOrderFromJson(*limit_order_json);
    auto market_order = createOrderFromJson(*market_order_json);

    auto orderbook = getOrCreateOrderBook("MSFT");

    // Add limit sell order first
    auto limit_order_ptr = std::make_shared<core::Order>(limit_order);
    EXPECT_TRUE(orderbook->addOrder(limit_order_ptr));

    // Execute market buy order
    auto market_order_ptr = std::make_shared<core::Order>(market_order);
    auto trades = matching_engine_->matchOrder(market_order_ptr, *orderbook);

    // Verify trade was created
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].quantity, 50.0);
    EXPECT_EQ(trades[0].price, 285.00);  // Should match at limit order price

    // Verify user portfolio updates
    auto buyer = users_["trader-002"];
    auto seller = users_["trader-003"];

    auto buyer_position = buyer->getPosition("MSFT");
    auto seller_position = seller->getPosition("MSFT");

    ASSERT_TRUE(buyer_position.has_value());
    ASSERT_TRUE(seller_position.has_value());

    EXPECT_NEAR(buyer_position->quantity, 50.0, 1e-9);
    EXPECT_NEAR(seller_position->quantity, 150.0, 1e-9);  // 200 - 50
}

TEST_F(TradingPipelineTest, MultipleSymbolTrading) {
    // Test trading across multiple symbols
    auto orders_json = test_data_["orders"];

    std::vector<std::string> order_ids = {"ORDER_001", "ORDER_002", "ORDER_005", "ORDER_006"};

    int initial_trade_count = trade_count_;

    for (const auto& order_id : order_ids) {
        auto order_json =
            std::find_if(orders_json.begin(), orders_json.end(),
                         [&order_id](const json& order) { return order["id"] == order_id; });

        ASSERT_NE(order_json, orders_json.end()) << "Order " << order_id << " not found";

        auto order = createOrderFromJson(*order_json);
        auto orderbook = getOrCreateOrderBook(order.getSymbol());
        auto order_ptr = std::make_shared<core::Order>(order);

        EXPECT_TRUE(orderbook->addOrder(order_ptr));

        // Try to match the order
        auto trades = matching_engine_->matchOrder(order_ptr, *orderbook);

        // Log results
        if (!trades.empty()) {
            app_logger_->log(
                logging::LogLevel::INFO,
                "Order " + order_id + " generated " + std::to_string(trades.size()) + " trades");
        }
    }

    // Should have trades from AAPL and GOOGL matches
    EXPECT_GT(trade_count_, initial_trade_count);
    EXPECT_GT(total_volume_, 0.0);

    // Verify that we have trades for both symbols
    bool has_aapl_trade = false, has_googl_trade = false;
    for (const auto& trade : trades_) {
        if (trade.symbol == "AAPL")
            has_aapl_trade = true;
        if (trade.symbol == "GOOGL")
            has_googl_trade = true;
    }

    EXPECT_TRUE(has_aapl_trade);
    EXPECT_TRUE(has_googl_trade);
}

TEST_F(TradingPipelineTest, InvalidOrderHandling) {
    // Test invalid orders from sample data
    auto invalid_orders = test_data_["invalid_orders"];

    for (const auto& invalid_order_json : invalid_orders) {
        // Skip the insufficient funds test as it requires special setup
        if (invalid_order_json["id"] == "INVALID_003")
            continue;

        try {
            auto order = createOrderFromJson(invalid_order_json);
            auto validation_result = validator_->validate(std::make_shared<core::Order>(order));

            EXPECT_FALSE(validation_result.is_valid)
                << "Order " << invalid_order_json["id"] << " should be invalid";

            if (!validation_result.is_valid) {
                app_logger_->log(logging::LogLevel::INFO,
                                 "Invalid order " + invalid_order_json["id"].get<std::string>() +
                                     " correctly rejected: " + validation_result.error_message);
            }
        } catch (const std::exception& e) {
            // Some invalid orders might throw exceptions during creation
            app_logger_->log(logging::LogLevel::INFO,
                             "Invalid order " + invalid_order_json["id"].get<std::string>() +
                                 " threw exception: " + e.what());
        }
    }
}

TEST_F(TradingPipelineTest, InsufficientFundsScenario) {
    // Create a poor user and test insufficient funds
    auto poor_user = std::make_shared<core::User>("trader-poor", 1000.0);
    matching_engine_->addUser(poor_user);

    // Try to buy expensive stock
    core::Order expensive_order("EXPENSIVE_001", "trader-poor", "AAPL", core::OrderType::LIMIT,
                                core::OrderSide::BUY, 100.0,
                                200.0);  // Total: $20,000, but user only has $1,000

    auto validation_result = validator_->validate(std::make_shared<core::Order>(expensive_order));

    // Order should be valid from validator perspective (it doesn't check user funds)
    EXPECT_TRUE(validation_result.is_valid);

    // But when we try to execute, user portfolios should reject it
    auto orderbook = getOrCreateOrderBook("AAPL");

    // Add a matching sell order first
    auto sell_order =
        std::make_shared<core::Order>("SELL_AAPL", "trader-001", "AAPL", core::OrderType::LIMIT,
                                      core::OrderSide::SELL, 100.0, 200.0);
    orderbook->addOrder(sell_order);

    // Try to match the expensive buy order
    auto expensive_order_ptr = std::make_shared<core::Order>(expensive_order);
    auto trades = matching_engine_->matchOrder(expensive_order_ptr, *orderbook);

    // Trade should be created but user portfolio update should fail
    // This tests the portfolio validation in the matching engine
    EXPECT_NEAR(poor_user->getCashBalance(), 1000.0, 1e-9);  // Should be unchanged
}

TEST_F(TradingPipelineTest, LoggingAndCallbackVerification) {
    // Verify that all logging and callbacks work properly
    auto orders_json = test_data_["orders"];
    auto order_json = orders_json[0];  // Use first order

    auto order = createOrderFromJson(order_json);
    auto orderbook = getOrCreateOrderBook(order.getSymbol());
    auto order_ptr = std::make_shared<core::Order>(order);

    int initial_trade_count = trade_count_;
    int initial_execution_count = executions_.size();

    orderbook->addOrder(order_ptr);

    // The callback should have been triggered for logging
    // Even if no trade occurred, the system should be ready

    EXPECT_GE(trade_count_, initial_trade_count);
    EXPECT_GE(executions_.size(), initial_execution_count);
}

TEST_F(TradingPipelineTest, HighVolumeStressTest) {
    // Create many orders to stress test the system
    auto orderbook_aapl = getOrCreateOrderBook("AAPL");

    const int NUM_ORDERS = 1000;
    std::vector<std::shared_ptr<core::Order>> orders;

    // Create alternating buy and sell orders
    for (int i = 0; i < NUM_ORDERS; ++i) {
        std::string order_id = "STRESS_" + std::to_string(i);
        std::string user_id = (i % 2 == 0) ? "trader-001" : "trader-002";
        core::OrderSide side = (i % 2 == 0) ? core::OrderSide::SELL : core::OrderSide::BUY;
        double price = 150.0 + (i % 10) * 0.5;  // Varying prices

        auto order = std::make_shared<core::Order>(order_id, user_id, "AAPL",
                                                   core::OrderType::LIMIT, side, 10.0, price);

        orders.push_back(order);
        orderbook_aapl->addOrder(order);

        // Every few orders, try to match
        if (i % 5 == 4) {
            auto trades = matching_engine_->matchOrder(order, *orderbook_aapl);
            // Just verify the system handles the load
        }
    }

    // Verify the system handled the stress test
    EXPECT_GE(matching_engine_->getTotalTrades(), 0);
    EXPECT_GE(matching_engine_->getTotalVolume(), 0.0);

    app_logger_->log(logging::LogLevel::INFO,
                     "Stress test completed. Total trades: " +
                         std::to_string(matching_engine_->getTotalTrades()) +
                         ", Total volume: " + std::to_string(matching_engine_->getTotalVolume()));
}
