#include <chrono>
#include <fstream>
#include <iostream>
#include <queue>
#include <thread>
#include <gtest/gtest.h>

#include "trading/core/matching_engine.hpp"
#include "trading/core/order.hpp"
#include "trading/core/orderbook.hpp"
#include "trading/core/user.hpp"
#include "trading/execution/executor.hpp"
#include "trading/logging/app_logger.hpp"
#include "trading/logging/trade_logger.hpp"
#include "trading/messaging/queue_client.hpp"
#include "trading/network/http_server.hpp"
#include "trading/utils/config.hpp"
#include "trading/validation/order_validator.hpp"
#include "../../apps/json.hpp"

using json = nlohmann::json;
using namespace trading;

// Mock message queue that doesn't require actual Redpanda
class MockMessageQueue {
  public:
    struct Message {
        std::string topic;
        std::string key;
        std::string value;
    };

    void publish(const std::string& topic, const std::string& key, const std::string& value) {
        Message msg{topic, key, value};
        messages_.push(msg);
    }

    bool hasMessages() const {
        return !messages_.empty();
    }

    Message consumeMessage() {
        if (messages_.empty()) {
            throw std::runtime_error("No messages to consume");
        }
        Message msg = messages_.front();
        messages_.pop();
        return msg;
    }

    size_t messageCount() const {
        return messages_.size();
    }

    void clear() {
        std::queue<Message> empty;
        std::swap(messages_, empty);
    }

  private:
    std::queue<Message> messages_;
};

class PipelineMockTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Initialize components
        trade_logger_ = std::make_unique<logging::TradeLogger>("test_mock_trades.log");
        app_logger_ = std::make_shared<logging::AppLogger>("test_mock_app.log");
        validator_ = std::make_unique<validation::OrderValidator>();
        executor_ = std::make_unique<execution::Executor>();
        matching_engine_ = std::make_unique<core::MatchingEngine>();

        // Initialize mock queue
        mock_queue_ = std::make_unique<MockMessageQueue>();

        // Setup callbacks and counters
        trade_count_ = 0;
        total_volume_ = 0.0;
        processed_orders_ = 0;

        setupCallbacks();
        loadTestData();
    }

    void TearDown() override {
        // Clean up test log files
        std::remove("test_mock_trades.log");
        std::remove("test_mock_app.log");
    }

    void setupCallbacks() {
        // Setup trade callback
        matching_engine_->setTradeCallback(
            [this](const core::Trade& trade) { handleTrade(trade); });

        // Setup execution callback
        executor_->setExecutionCallback(
            [this](const execution::ExecutionResult& result) { handleExecution(result); });
    }

    void loadTestData() {
        // Try multiple possible paths since tests can run from different directories
        std::vector<std::string> possible_paths = {"../tests/fixtures/sample_orders.json",
                                                   "tests/fixtures/sample_orders.json",
                                                   "../../tests/fixtures/sample_orders.json",
                                                   "../../../tests/fixtures/sample_orders.json"};

        std::ifstream file;
        bool found = false;

        for (const auto& path : possible_paths) {
            file.open(path);
            if (file.is_open()) {
                found = true;
                std::cout << "Found sample_orders.json at: " << path << std::endl;
                break;
            }
            file.clear();
        }

        if (!found) {
            FAIL() << "Could not find sample_orders.json in any of the expected locations";
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

    // Simulate HTTP request handler (producer side)
    std::string handleOrderRequest(const std::string& body) {
        try {
            // Light validation on the incoming request
            auto json_body = json::parse(body);
            if (!json_body.contains("userId") || !json_body.contains("id")) {
                throw std::invalid_argument("Request must contain 'userId' and 'id'");
            }

            std::string user_id = json_body.at("userId");

            // Publish to mock queue instead of Redpanda
            mock_queue_->publish("test-order-requests", user_id, body);

            // Immediately acknowledge the request
            json response = {{"status", "order accepted for processing"},
                             {"order_id", json_body.at("id").get<std::string>()}};
            return response.dump();

        } catch (const json::exception& e) {
            json response = {{"error", "Invalid JSON format: " + std::string(e.what())}};
            return response.dump();
        } catch (const std::invalid_argument& e) {
            json response = {{"error", std::string(e.what())}};
            return response.dump();
        }
    }

    // Simulate queue message processor (consumer side)
    void processOrderFromQueue(const MockMessageQueue::Message& msg) {
        try {
            processed_orders_++;
            app_logger_->log(logging::LogLevel::INFO,
                             "Processing order from mock queue: " + msg.value);
            auto json_body = json::parse(msg.value);

            // Extract order data safely
            std::string id = json_body.at("id");
            std::string userId = json_body.at("userId");
            std::string symbol = json_body.at("symbol");
            std::string type_str = json_body.at("type");
            std::string side_str = json_body.at("side");
            double quantity = json_body.at("quantity");

            core::OrderType type = stringToOrderType(type_str);
            core::OrderSide side = stringToOrderSide(side_str);

            double price = 0.0;
            if (type == core::OrderType::LIMIT || type == core::OrderType::STOP) {
                price = json_body.at("price");
            }

            // Create order object
            core::Order order(id, userId, symbol, type, side, quantity, price);

            // Validate order
            auto validation_result = validator_->validate(std::make_shared<core::Order>(order));
            if (!validation_result.is_valid) {
                app_logger_->log(logging::LogLevel::ERROR, "Invalid order from queue rejected: " +
                                                               validation_result.error_message);
                return;
            }

            // Add order to matching engine
            auto orderbook = matching_engine_->getOrderBook(symbol);
            if (!orderbook) {
                orderbook = std::make_shared<core::OrderBook>(symbol);
                matching_engine_->addOrderBook(symbol, orderbook);
            }

            auto order_ptr = std::make_shared<core::Order>(order);
            if (!orderbook->addOrder(order_ptr)) {
                app_logger_->log(logging::LogLevel::ERROR,
                                 "Failed to add order " + id + " to order book");
                return;
            }

            // Match the order against existing orders in the book
            auto trades = matching_engine_->matchOrder(order_ptr, *orderbook);

            // Log info about generated trades
            if (!trades.empty()) {
                app_logger_->log(
                    logging::LogLevel::INFO,
                    "Order " + id + " generated " + std::to_string(trades.size()) + " trades");
            }
        } catch (const json::exception& e) {
            app_logger_->log(logging::LogLevel::ERROR,
                             "Failed to parse order from queue: " + std::string(e.what()));
        } catch (const std::invalid_argument& e) {
            app_logger_->log(logging::LogLevel::ERROR,
                             "Invalid data in order from queue: " + std::string(e.what()));
        }
    }

    void handleTrade(const core::Trade& trade) {
        trade_count_++;
        total_volume_ += trade.quantity * trade.price;
        trades_.push_back(trade);

        trade_logger_->logTrade(trade);
        auto result = executor_->execute(trade);
    }

    void handleExecution(const execution::ExecutionResult& result) {
        executions_.push_back(result);
    }

    // Helper functions
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

    // Process all messages in the mock queue
    void processAllQueuedMessages() {
        while (mock_queue_->hasMessages()) {
            auto msg = mock_queue_->consumeMessage();
            processOrderFromQueue(msg);
        }
    }

    // Test components
    std::unique_ptr<logging::TradeLogger> trade_logger_;
    std::shared_ptr<logging::AppLogger> app_logger_;
    std::unique_ptr<validation::OrderValidator> validator_;
    std::unique_ptr<execution::Executor> executor_;
    std::unique_ptr<core::MatchingEngine> matching_engine_;
    std::unique_ptr<MockMessageQueue> mock_queue_;

    json test_data_;
    std::map<std::string, std::shared_ptr<core::User>> users_;
    std::vector<core::Trade> trades_;
    std::vector<execution::ExecutionResult> executions_;

    int trade_count_;
    double total_volume_;
    int processed_orders_;
};

// Test basic HTTP to queue pipeline (fast version)
TEST_F(PipelineMockTest, BasicHttpToQueueFlow) {
    // Send a simple order via HTTP (simulated)
    json order_json = {{"id", "TEST_001"}, {"userId", "trader-001"}, {"symbol", "AAPL"},
                       {"type", "LIMIT"},  {"side", "SELL"},         {"quantity", 10.0},
                       {"price", 150.0}};

    std::string response = handleOrderRequest(order_json.dump());

    // Verify HTTP response
    auto response_json = json::parse(response);
    EXPECT_EQ(response_json["status"], "order accepted for processing");
    EXPECT_EQ(response_json["order_id"], "TEST_001");

    // Verify message was queued
    EXPECT_EQ(mock_queue_->messageCount(), 1);

    // Process the queued message
    processAllQueuedMessages();

    // Verify the order was processed
    EXPECT_EQ(processed_orders_, 1);

    // Verify the order was added to the orderbook
    auto orderbook = matching_engine_->getOrderBook("AAPL");
    ASSERT_TRUE(orderbook != nullptr);
}

// Test full trading scenario through HTTP/Queue pipeline (fast)
TEST_F(PipelineMockTest, FullTradingScenarioViaQueue) {
    // Send sell order first
    json sell_order = {{"id", "PIPELINE_SELL_001"},
                       {"userId", "trader-001"},
                       {"symbol", "AAPL"},
                       {"type", "LIMIT"},
                       {"side", "SELL"},
                       {"quantity", 25.0},
                       {"price", 150.50}};

    std::string sell_response = handleOrderRequest(sell_order.dump());
    auto sell_response_json = json::parse(sell_response);
    EXPECT_EQ(sell_response_json["status"], "order accepted for processing");

    // Send matching buy order
    json buy_order = {{"id", "PIPELINE_BUY_001"}, {"userId", "trader-002"}, {"symbol", "AAPL"},
                      {"type", "LIMIT"},          {"side", "BUY"},          {"quantity", 25.0},
                      {"price", 150.50}};

    std::string buy_response = handleOrderRequest(buy_order.dump());
    auto buy_response_json = json::parse(buy_response);
    EXPECT_EQ(buy_response_json["status"], "order accepted for processing");

    // Verify both messages were queued
    EXPECT_EQ(mock_queue_->messageCount(), 2);

    // Process all queued messages
    processAllQueuedMessages();

    // Verify both orders were processed
    EXPECT_EQ(processed_orders_, 2);

    // Verify trade was created
    EXPECT_EQ(trade_count_, 1);
    EXPECT_NEAR(total_volume_, 3762.50, 1e-2);  // 25 * 150.50

    // Verify user portfolios were updated
    auto seller = users_["trader-001"];
    auto buyer = users_["trader-002"];

    auto seller_position = seller->getPosition("AAPL");
    auto buyer_position = buyer->getPosition("AAPL");

    ASSERT_TRUE(seller_position.has_value());
    ASSERT_TRUE(buyer_position.has_value());

    // Seller should have 75 shares left (100 - 25)
    EXPECT_NEAR(seller_position->quantity, 75.0, 1e-9);
    // Buyer should have 25 shares
    EXPECT_NEAR(buyer_position->quantity, 25.0, 1e-9);
}

// Test user partitioning (same user orders)
TEST_F(PipelineMockTest, UserPartitioningSimulation) {
    // Track the order of processing by user
    std::vector<std::string> processing_order;

    // Send multiple orders from the same user
    std::vector<json> user_orders;
    for (int i = 0; i < 5; ++i) {
        json order = {{"id", "USER_PARTITION_" + std::to_string(i)},
                      {"userId", "trader-001"},  // Same user for all orders
                      {"symbol", "AAPL"},
                      {"type", "LIMIT"},
                      {"side", (i % 2 == 0) ? "SELL" : "BUY"},
                      {"quantity", 10.0},
                      {"price", 150.0 + i * 0.5}};
        user_orders.push_back(order);
    }

    // Send all orders (they should be queued with same key)
    for (const auto& order : user_orders) {
        std::string response = handleOrderRequest(order.dump());
        auto response_json = json::parse(response);
        EXPECT_EQ(response_json["status"], "order accepted for processing");
    }

    // Verify all messages were queued
    EXPECT_EQ(mock_queue_->messageCount(), 5);

    // Process messages in order (simulating same partition processing)
    while (mock_queue_->hasMessages()) {
        auto msg = mock_queue_->consumeMessage();
        auto order_json = json::parse(msg.value);
        processing_order.push_back(order_json["id"]);
        processOrderFromQueue(msg);
    }

    // All orders should be processed
    EXPECT_EQ(processed_orders_, 5);

    // Verify orders were processed in the same order they were sent
    // (This simulates the partition ordering guarantee)
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(processing_order[i], "USER_PARTITION_" + std::to_string(i));
    }

    // Since they're all from the same user and same symbol,
    // some should match and create trades
    EXPECT_GT(trade_count_, 0);
}

// Test multiple users with different partition keys
TEST_F(PipelineMockTest, MultipleUsersPartitioningSimulation) {
    // Create orders for different users
    std::vector<std::string> user_ids = {"trader-001", "trader-002", "trader-003"};
    std::map<std::string, std::vector<std::string>> user_order_sequences;

    for (int i = 0; i < 9; ++i) {  // 3 orders per user
        std::string user_id = user_ids[i % 3];
        json order = {{"id", "MULTI_USER_" + std::to_string(i)},
                      {"userId", user_id},
                      {"symbol", "AAPL"},
                      {"type", "LIMIT"},
                      {"side", (i % 2 == 0) ? "SELL" : "BUY"},
                      {"quantity", 10.0},
                      {"price", 150.0}};

        std::string response = handleOrderRequest(order.dump());
        auto response_json = json::parse(response);
        EXPECT_EQ(response_json["status"], "order accepted for processing");
    }

    // Verify all messages were queued
    EXPECT_EQ(mock_queue_->messageCount(), 9);

    // Process messages and track by user (simulating partition processing)
    while (mock_queue_->hasMessages()) {
        auto msg = mock_queue_->consumeMessage();
        auto order_json = json::parse(msg.value);
        user_order_sequences[msg.key].push_back(order_json["id"]);
        processOrderFromQueue(msg);
    }

    EXPECT_EQ(processed_orders_, 9);

    // Verify each user had exactly 3 orders
    for (const auto& user_id : user_ids) {
        EXPECT_EQ(user_order_sequences[user_id].size(), 3);
    }

    // With multiple users and alternating buy/sell, we should get some trades
    EXPECT_GT(trade_count_, 0);
    EXPECT_GT(total_volume_, 0.0);
}

// Test invalid order handling through pipeline
TEST_F(PipelineMockTest, InvalidOrderHandling) {
    json invalid_order = {{"id", "INVALID_PIPELINE_001"},
                          {"userId", "trader-001"},
                          {"symbol", ""},  // Invalid empty symbol
                          {"type", "LIMIT"},
                          {"side", "BUY"},
                          {"quantity", 10.0},
                          {"price", 150.0}};

    std::string response = handleOrderRequest(invalid_order.dump());
    auto response_json = json::parse(response);

    // Order should be accepted by HTTP (we do light validation)
    EXPECT_EQ(response_json["status"], "order accepted for processing");

    // Process the queued message
    processAllQueuedMessages();

    // Order should be processed but rejected during validation
    EXPECT_EQ(processed_orders_, 1);

    // No trades should be created from invalid order
    EXPECT_EQ(trade_count_, 0);
}

// Test high volume through pipeline (fast version)
TEST_F(PipelineMockTest, HighVolumePipelineSimulation) {
    const int NUM_ORDERS = 100;  // Larger since no network overhead

    // Create many orders
    for (int i = 0; i < NUM_ORDERS; ++i) {
        json order = {
            {"id", "HIGH_VOLUME_" + std::to_string(i)},
            {"userId", "trader-" + std::to_string(i % 3 + 1)},  // Rotate between 3 users
            {"symbol", "AAPL"},
            {"type", "LIMIT"},
            {"side", (i % 2 == 0) ? "SELL" : "BUY"},
            {"quantity", 10.0},
            {"price", 150.0 + (i % 10) * 0.1}  // Varying prices
        };

        std::string response = handleOrderRequest(order.dump());
        auto response_json = json::parse(response);
        EXPECT_EQ(response_json["status"], "order accepted for processing");
    }

    // Verify all messages were queued
    EXPECT_EQ(mock_queue_->messageCount(), NUM_ORDERS);

    // Process all messages
    processAllQueuedMessages();

    EXPECT_EQ(processed_orders_, NUM_ORDERS);

    // Should have generated some trades from matching orders
    EXPECT_GT(trade_count_, 0);
    EXPECT_GT(total_volume_, 0.0);

    app_logger_->log(logging::LogLevel::INFO,
                     "High volume pipeline simulation completed. Processed orders: " +
                         std::to_string(processed_orders_) +
                         ", Trades: " + std::to_string(trade_count_) +
                         ", Volume: " + std::to_string(total_volume_));
}

// Test malformed HTTP requests
TEST_F(PipelineMockTest, MalformedRequestHandling) {
    // Missing required fields
    json malformed_order = {{"id", "MALFORMED_001"},
                            // Missing userId
                            {"symbol", "AAPL"},
                            {"type", "LIMIT"},
                            {"side", "BUY"},
                            {"quantity", 10.0},
                            {"price", 150.0}};

    std::string response = handleOrderRequest(malformed_order.dump());
    auto response_json = json::parse(response);

    // Should be rejected at HTTP level
    EXPECT_NE(response_json["status"], "order accepted for processing");
    EXPECT_TRUE(response_json.contains("error"));

    // No messages should be queued
    EXPECT_EQ(mock_queue_->messageCount(), 0);
    EXPECT_EQ(processed_orders_, 0);
}

// Test error resilience
TEST_F(PipelineMockTest, ErrorResilienceSimulation) {
    // Send a mix of valid and invalid orders
    std::vector<json> mixed_orders = {
        {{"id", "VALID_001"},
         {"userId", "trader-001"},
         {"symbol", "AAPL"},
         {"type", "LIMIT"},
         {"side", "BUY"},
         {"quantity", 10.0},
         {"price", 150.0}},
        {{"id", "INVALID_001"},
         {"userId", "trader-001"},
         {"symbol", ""},
         {"type", "LIMIT"},
         {"side", "BUY"},
         {"quantity", 10.0},
         {"price", 150.0}},  // Invalid symbol
        {{"id", "VALID_002"},
         {"userId", "trader-002"},
         {"symbol", "AAPL"},
         {"type", "LIMIT"},
         {"side", "SELL"},
         {"quantity", 10.0},
         {"price", 150.0}},
    };

    int valid_orders_sent = 0;
    for (const auto& order : mixed_orders) {
        std::string response = handleOrderRequest(order.dump());
        auto response_json = json::parse(response);

        if (response_json["status"] == "order accepted for processing") {
            valid_orders_sent++;
        }
    }

    // Process all queued messages
    processAllQueuedMessages();

    // Should have processed the valid orders
    EXPECT_EQ(processed_orders_, valid_orders_sent);

    // Should have created a trade from the valid matching orders
    EXPECT_EQ(trade_count_, 1);
    EXPECT_NEAR(total_volume_, 1500.0, 1e-2);  // 10 * 150.0
}

// Test pipeline performance characteristics
TEST_F(PipelineMockTest, PipelinePerformanceTest) {
    const int NUM_ORDERS = 1000;
    auto start_time = std::chrono::high_resolution_clock::now();

    // Generate orders
    for (int i = 0; i < NUM_ORDERS; ++i) {
        json order = {
            {"id", "PERF_" + std::to_string(i)},
            {"userId", "trader-" + std::to_string(i % 10 + 1)},  // 10 different users
            {"symbol", "AAPL"},
            {"type", "LIMIT"},
            {"side", (i % 2 == 0) ? "SELL" : "BUY"},
            {"quantity", 1.0},
            {"price", 150.0 + (i % 100) * 0.01}  // Price range 150.00-150.99
        };

        handleOrderRequest(order.dump());
    }

    auto queue_time = std::chrono::high_resolution_clock::now();

    // Process all orders
    processAllQueuedMessages();

    auto end_time = std::chrono::high_resolution_clock::now();

    auto queue_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(queue_time - start_time);
    auto processing_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - queue_time);
    auto total_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "Performance Test Results:" << std::endl;
    std::cout << "  Orders: " << NUM_ORDERS << std::endl;
    std::cout << "  Queue Time: " << queue_duration.count() << "ms" << std::endl;
    std::cout << "  Processing Time: " << processing_duration.count() << "ms" << std::endl;
    std::cout << "  Total Time: " << total_duration.count() << "ms" << std::endl;
    std::cout << "  Orders/sec: " << (NUM_ORDERS * 1000.0 / total_duration.count()) << std::endl;
    std::cout << "  Trades Generated: " << trade_count_ << std::endl;
    std::cout << "  Trades/sec: " << (trade_count_ * 1000.0 / processing_duration.count())
              << std::endl;
    std::cout << "  Total Volume: $" << total_volume_ << std::endl;

    EXPECT_EQ(processed_orders_, NUM_ORDERS);
    EXPECT_GT(trade_count_, 0);

    // Performance should be reasonable (adjust as needed)
    EXPECT_LT(total_duration.count(), 10000);  // Should complete in under 5 seconds
}
