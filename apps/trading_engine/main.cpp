#include "trading/core/matching_engine.hpp"
#include "trading/core/order.hpp"
#include "trading/core/orderbook.hpp"
#include "trading/execution/executor.hpp"
#include "trading/logging/app_logger.hpp"
#include "trading/logging/trade_logger.hpp"
#include "trading/messaging/queue_client.hpp"
#include "trading/network/http_server.hpp"
#include "trading/utils/config.hpp"
#include "trading/validation/order_validator.hpp"
#include "../json.hpp"
using json = nlohmann::json;

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <signal.h>

namespace {
trading::core::OrderType stringToOrderType(const std::string& type_str) {
    if (type_str == "LIMIT")
        return trading::core::OrderType::LIMIT;
    if (type_str == "MARKET")
        return trading::core::OrderType::MARKET;
    if (type_str == "STOP")
        return trading::core::OrderType::STOP;
    throw std::invalid_argument("Invalid order type string: " + type_str);
}

trading::core::OrderSide stringToOrderSide(const std::string& side_str) {
    if (side_str == "BUY")
        return trading::core::OrderSide::BUY;
    if (side_str == "SELL")
        return trading::core::OrderSide::SELL;
    throw std::invalid_argument("Invalid order side string: " + side_str);
}
}  // namespace

using namespace trading;

class TradingEngine {
  public:
    TradingEngine()
        : config_(std::make_shared<utils::Config>()),
          trade_logger_(std::make_shared<logging::TradeLogger>("trading_engine.log")),
          app_logger_(std::make_shared<logging::AppLogger>("app.log")),
          validator_(std::make_shared<validation::OrderValidator>()),
          executor_(std::make_shared<execution::Executor>()),
          matching_engine_(std::make_shared<core::MatchingEngine>()),
          http_server_(nullptr),
          queue_client_(nullptr),
          running_(false) {
    }

    bool initialize(const std::string& config_file) {
        // Load configuration
        if (!config_->loadFromFile(config_file)) {
            app_logger_->log(logging::LogLevel::ERROR,
                             "Failed to load configuration from: " + config_file);
            return false;
        }

        // Initialize HTTP server
        std::string host = config_->getString("http.host", "0.0.0.0");
        int port = config_->getInt("http.port", 8080);
        http_server_ = std::make_unique<network::HttpServer>(host, port);

        // Initialize queue client
        std::string brokers = config_->getString("redpanda.brokers", "localhost:9092");
        queue_client_ = std::make_unique<messaging::QueueClient>(brokers, app_logger_);

        // Setup callbacks
        setupCallbacks();

        trade_logger_->logMessage(logging::LogLevel::INFO,
                                  "Trading engine initialized successfully");
        return true;
    }

    bool start() {
        if (running_) {
            return false;
        }

        // Start HTTP server
        if (!http_server_->start()) {
            trade_logger_->logMessage(logging::LogLevel::ERROR, "Failed to start HTTP server");
            return false;
        }

        // Connect to message queue
        if (!queue_client_->connect()) {
            trade_logger_->logMessage(logging::LogLevel::ERROR,
                                      "Failed to connect to message queue");
            return false;
        }

        running_ = true;
        trade_logger_->logMessage(logging::LogLevel::INFO, "Trading engine started");
        return true;
    }

    void stop() {
        if (!running_) {
            return;
        }

        running_ = false;

        if (http_server_) {
            http_server_->stop();
        }

        if (queue_client_) {
            queue_client_->disconnect();
        }

        trade_logger_->logMessage(logging::LogLevel::INFO, "Trading engine stopped");
    }

    bool isRunning() const {
        return running_;
    }

  private:
    void setupCallbacks() {
        // Setup HTTP request handlers
        http_server_->setOrderHandler(
            [this](const network::HttpRequest& request) { return handleOrderRequest(request); });

        http_server_->setHealthHandler(
            [this](const network::HttpRequest& request) { return handleHealthRequest(request); });

        // Setup trade callback
        matching_engine_->setTradeCallback(
            [this](const core::Trade& trade) { handleTrade(trade); });

        // Setup execution callback
        executor_->setExecutionCallback(
            [this](const execution::ExecutionResult& result) { handleExecution(result); });
    }

    network::HttpResponse handleOrderRequest(const network::HttpRequest& request) {
        try {
            // Parse json string
            auto json_body = json::parse(request.body);

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

            // Process order

            // Create and send confirmation
            network::HttpResponse response;
            response.status_code = 200;
            response.body = "{\"status\": \"order received\", \"order_id\": \"" + id + "\"}";
            response.headers["Content-Type"] = "application/json";
            return response;

        } catch (const json::exception& e) {
            // Catches all JSON-related errors (parse, .at(), type conversion)
            network::HttpResponse response;
            response.status_code = 400;
            response.body = "{\"error\": \"Invalid JSON format or missing/invalid field: " +
                            std::string(e.what()) + "\"}";
            response.headers["Content-Type"] = "application/json";
            return response;
        } catch (const std::invalid_argument& e) {
            // Catches errors from our string-to-enum helpers
            network::HttpResponse response;
            response.status_code = 400;
            response.body =
                "{\"error\": \"Invalid value for type or side: " + std::string(e.what()) + "\"}";
            response.headers["Content-Type"] = "application/json";
            return response;
        }
    }

    network::HttpResponse handleHealthRequest(const network::HttpRequest& request) {
        (void)request;
        network::HttpResponse response;
        response.status_code = 200;
        response.body = std::string("{\"status\": \"healthy\", \"running\": ") +
                        (running_ ? "true" : "false") + "}";
        response.headers["Content-Type"] = "application/json";
        return response;
    }

    void handleTrade(const core::Trade& trade) {
        trade_logger_->logTrade(trade);

        // Execute the trade
        execution::ExecutionResult result = executor_->execute(trade);

        // Create and send confirmation
        auto confirmation = trade_logger_->createConfirmation(trade);
        trade_logger_->sendConfirmation(confirmation);
    }

    void handleExecution(const execution::ExecutionResult& result) {
        trade_logger_->logExecution(result);
    }

  private:
    std::shared_ptr<utils::Config> config_;
    std::shared_ptr<logging::TradeLogger> trade_logger_;
    std::shared_ptr<logging::AppLogger> app_logger_;
    std::shared_ptr<validation::OrderValidator> validator_;
    std::shared_ptr<execution::Executor> executor_;
    std::shared_ptr<core::MatchingEngine> matching_engine_;
    std::unique_ptr<network::HttpServer> http_server_;
    std::unique_ptr<messaging::QueueClient> queue_client_;
    bool running_;
};

// Global instance for signal handling
TradingEngine* g_engine = nullptr;
std::shared_ptr<logging::AppLogger> g_logger = nullptr;

void signalHandler(int signal) {
    if (g_engine) {
        if (g_logger) {
            g_logger->log(logging::LogLevel::INFO,
                          "Received signal " + std::to_string(signal) + ", shutting down...");
        }
        g_engine->stop();
    }
}

int main(int argc, char* argv[]) {
    std::string config_file = "config/trading_engine.json";

    if (argc > 1) {
        config_file = argv[1];
    }

    // Create trading engine
    TradingEngine engine;
    g_engine = &engine;

    // A kludge to get the logger to the signal handler
    g_logger = std::make_shared<logging::AppLogger>("app.log");

    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Initialize engine
    if (!engine.initialize(config_file)) {
        g_logger->log(logging::LogLevel::ERROR, "Failed to initialize trading engine");
        return 1;
    }

    // Start engine
    if (!engine.start()) {
        g_logger->log(logging::LogLevel::ERROR, "Failed to start trading engine");
        return 1;
    }

    g_logger->log(logging::LogLevel::INFO, "Trading engine started. Press Ctrl+C to stop.");

    // Main loop
    while (engine.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    g_logger->log(logging::LogLevel::INFO, "Trading engine stopped.");
    return 0;
}