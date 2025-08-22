#include "trading/core/matching_engine.hpp"
#include "trading/core/order.hpp"
#include "trading/core/orderbook.hpp"
#include "trading/execution/executor.hpp"
#include "trading/logging/app_logger.hpp"
#include "trading/logging/trade_logger.hpp"
#include "trading/messaging/queue_client.hpp"
#include "trading/network/http_server.hpp"
#include "trading/statistics/statistics_collector.hpp"
#include "trading/utils/config.hpp"
#include "trading/validation/order_validator.hpp"
#include "../json.hpp"
using json = nlohmann::json;

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
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
          stats_collector_(nullptr),
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
        int threads = config_->getInt("http.threads", 4);
        http_server_ = std::make_unique<network::HttpServer>(host, port, threads);

        // Initialize queue client
        std::string brokers = config_->getString("redpanda.brokers", "localhost:9092");
        queue_client_ = std::make_unique<messaging::QueueClient>(brokers, app_logger_);

        // Initialize statistics collector
        statistics::StatisticsCollector::Config stats_config;
        stats_config.enabled = config_->getBool("statistics.enabled", true);
        stats_config.queue_capacity = config_->getInt("statistics.queue_capacity", 10000);
        stats_config.cleanup_interval =
            std::chrono::seconds(config_->getInt("statistics.cleanup_interval", 3600));

        // For now, use default timeframes - could be extended to parse comma-separated string
        // stats_config.timeframes defaults to {"1m", "1h", "1d"} as defined in the constructor

        stats_collector_ = std::make_unique<statistics::StatisticsCollector>(stats_config);

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

        // Start async logging threads
        try {
            app_logger_->start();
            trade_logger_->start();
        } catch (const std::runtime_error& e) {
            app_logger_->log(logging::LogLevel::ERROR,
                             "Failed to start logging threads: " + std::string(e.what()));
            return false;
        }

        // Start HTTP server
        if (!http_server_->start()) {
            trade_logger_->logMessage(logging::LogLevel::ERROR, "Failed to start HTTP server");
            return false;
        }

        // Start statistics collector
        if (!stats_collector_->start()) {
            trade_logger_->logMessage(logging::LogLevel::ERROR,
                                      "Failed to start statistics collector");
            return false;
        }

        // Connect to message queue
        if (!queue_client_->connect()) {
            trade_logger_->logMessage(logging::LogLevel::ERROR,
                                      "Failed to connect to message queue");
            return false;
        }

        // Setup queue message handler for processing orders from Redpanda
        if (!queue_client_->subscribe("order-requests", [this](const messaging::Message& msg) {
                processOrderFromQueue(msg);
            })) {
            trade_logger_->logMessage(logging::LogLevel::ERROR,
                                      "Failed to subscribe to order-requests topic");
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

        if (stats_collector_) {
            stats_collector_->stop();
        }

        if (queue_client_) {
            queue_client_->disconnect();
        }

        trade_logger_->logMessage(logging::LogLevel::INFO, "Trading engine stopped");

        // Stop async logging threads (this will flush all remaining messages)
        trade_logger_->stop();
        app_logger_->stop();
    }

    bool isRunning() const {
        return running_;
    }

  private:
    void setupCallbacks() {
        // Setup HTTP request handlers using new routing system
        http_server_->registerRoute("POST", "/order", [this](const network::HttpRequest& request) {
            return handleOrderRequest(request);
        });

        http_server_->registerRoute("GET", "/health", [this](const network::HttpRequest& request) {
            return handleHealthRequest(request);
        });

        http_server_->registerRoute("GET", "/api/v1/orderbook/{symbol}",
                                    [this](const network::HttpRequest& request) {
                                        return handleOrderBookRequest(request);
                                    });

        http_server_->registerRoute(
            "GET", "/api/v1/stats/{symbol}",
            [this](const network::HttpRequest& request) { return handleStatsRequest(request); });

        http_server_->registerRoute(
            "GET", "/api/v1/stats/{symbol}/{timeframe}",
            [this](const network::HttpRequest& request) { return handleStatsRequest(request); });

        http_server_->registerRoute(
            "GET", "/api/v1/stats/all",
            [this](const network::HttpRequest& request) { return handleAllStatsRequest(request); });

        http_server_->registerRoute("GET", "/api/v1/stats/summary",
                                    [this](const network::HttpRequest& request) {
                                        return handleStatsSummaryRequest(request);
                                    });

        http_server_->registerRoute("GET", "/api/v1/leaderboard",
                                    [this](const network::HttpRequest& request) {
                                        return handleLeaderboardRequest(request);
                                    });

        // Setup trade callback
        matching_engine_->setTradeCallback(
            [this](const core::Trade& trade) { handleTrade(trade); });

        // Setup execution callback
        executor_->setExecutionCallback(
            [this](const execution::ExecutionResult& result) { handleExecution(result); });
    }

    network::HttpResponse handleOrderRequest(const network::HttpRequest& request) {
        try {
            // Light validation on the incoming request
            auto json_body = json::parse(request.body);
            if (!json_body.contains("userId") || !json_body.contains("id")) {
                throw std::invalid_argument("Request must contain 'userId' and 'id'");
            }

            std::string user_id = json_body.at("userId");

            // Publish the full request body to Redpanda, using userId as the key
            bool published = queue_client_->publish("order-requests", user_id, request.body);

            if (!published) {
                app_logger_->log(logging::LogLevel::ERROR, "Failed to publish order to queue");
                network::HttpResponse response;
                response.status_code = 500;  // Internal Server Error
                response.body = "{\"error\": \"Failed to queue order for processing\"}";
                response.headers["Content-Type"] = "application/json";
                return response;
            }

            // Immediately acknowledge the request
            network::HttpResponse response;
            response.status_code = 202;  // Accepted
            response.body = "{\"status\": \"order accepted for processing\", \"order_id\": \"" +
                            json_body.at("id").get<std::string>() + "\"}";
            response.headers["Content-Type"] = "application/json";
            return response;

        } catch (const json::exception& e) {
            network::HttpResponse response;
            response.status_code = 400;
            response.body = "{\"error\": \"Invalid JSON format: " + std::string(e.what()) + "\"}";
            response.headers["Content-Type"] = "application/json";
            return response;
        } catch (const std::invalid_argument& e) {
            network::HttpResponse response;
            response.status_code = 400;
            response.body = "{\"error\": \"" + std::string(e.what()) + "\"}";
            response.headers["Content-Type"] = "application/json";
            return response;
        }
    }

    void processOrderFromQueue(const messaging::Message& msg) {
        try {
            auto json_body = json::parse(msg.value);

            // Extract order data safely
            std::string id = json_body.at("id");
            std::string userId = json_body.at("userId");
            std::string symbol = json_body.at("symbol");
            std::string type_str = json_body.at("type");
            std::string side_str = json_body.at("side");
            double quantity = json_body.at("quantity");

            // Log processing but without the full message body for performance
            app_logger_->log(logging::LogLevel::INFO, "Processing order from queue: " + id);

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
                // Optionally, publish to a "dead-letter" or "rejected-orders" topic
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

    network::HttpResponse handleHealthRequest(const network::HttpRequest& request) {
        (void)request;
        network::HttpResponse response;
        response.status_code = 200;
        response.body = std::string("{\"status\": \"healthy\", \"running\": ") +
                        (running_ ? "true" : "false") + "}";
        response.headers["Content-Type"] = "application/json";
        return response;
    }

    network::HttpResponse handleOrderBookRequest(const network::HttpRequest& request) {
        try {
            // Extract symbol from path parameters
            auto it = request.path_params.find("symbol");
            if (it == request.path_params.end()) {
                network::HttpResponse response;
                response.status_code = 400;
                response.body = "{\"error\": \"Symbol parameter is required\"}";
                response.headers["Content-Type"] = "application/json";
                return response;
            }

            std::string symbol = it->second;
            auto orderbook = matching_engine_->getOrderBook(symbol);

            if (!orderbook) {
                network::HttpResponse response;
                response.status_code = 404;
                response.body = "{\"error\": \"Order book not found for symbol: " + symbol + "\"}";
                response.headers["Content-Type"] = "application/json";
                return response;
            }

            // Get orderbook data as JSON
            std::string orderbook_json = orderbook->toJSON();

            network::HttpResponse response;
            response.status_code = 200;
            response.body = orderbook_json;
            response.headers["Content-Type"] = "application/json";
            return response;

        } catch (const std::exception& e) {
            network::HttpResponse response;
            response.status_code = 500;
            response.body =
                json({{"error", std::string("Internal server error: ") + e.what()}}).dump();
            response.headers["Content-Type"] = "application/json";
            return response;
        }
    }

    network::HttpResponse handleStatsRequest(const network::HttpRequest& request) {
        try {
            // Extract symbol from path parameters
            auto symbol_it = request.path_params.find("symbol");
            if (symbol_it == request.path_params.end()) {
                return createErrorResponse(400, "Missing symbol parameter");
            }
            std::string symbol = symbol_it->second;

            // Check for timeframe parameter
            auto timeframe_it = request.path_params.find("timeframe");

            if (!stats_collector_ || !stats_collector_->isRunning()) {
                return createErrorResponse(503, "Statistics collector not available");
            }

            // Get statistics for the symbol
            auto stats_opt = stats_collector_->getStatsForSymbol(symbol);
            if (!stats_opt.has_value()) {
                return createErrorResponse(404, "No statistics available for symbol: " + symbol);
            }

            json response_json;
            response_json["symbol"] = symbol;
            response_json["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count();

            if (timeframe_it != request.path_params.end()) {
                // Return specific timeframe data
                std::string timeframe = timeframe_it->second;
                auto& timeframes = stats_opt->timeframes;
                auto tf_it = timeframes.find(timeframe);

                if (tf_it == timeframes.end()) {
                    return createErrorResponse(404,
                                               "No data available for timeframe: " + timeframe);
                }

                response_json["timeframe"] = timeframe;
                response_json["data"] = tf_it->second.toJson();
                response_json["last_trade_price"] = stats_opt->last_trade_price;
            } else {
                // Return all timeframes
                response_json["data"] = stats_opt->toJson();
            }

            network::HttpResponse response;
            response.status_code = 200;
            response.body = response_json.dump();
            response.headers["Content-Type"] = "application/json";
            return response;

        } catch (const std::exception& e) {
            return createErrorResponse(500, "Internal server error: " + std::string(e.what()));
        }
    }

    network::HttpResponse handleAllStatsRequest(const network::HttpRequest& request) {
        (void)request;  // Suppress unused parameter warning
        try {
            if (!stats_collector_ || !stats_collector_->isRunning()) {
                return createErrorResponse(503, "Statistics collector not available");
            }

            auto all_stats = stats_collector_->getAllStats();

            json response_json;
            response_json["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count();
            response_json["total_symbols"] = all_stats.size();
            response_json["symbols"] = json::object();

            for (const auto& [symbol, stats] : all_stats) {
                response_json["symbols"][symbol] = stats.toJson();
            }

            network::HttpResponse response;
            response.status_code = 200;
            response.body = response_json.dump();
            response.headers["Content-Type"] = "application/json";
            return response;

        } catch (const std::exception& e) {
            return createErrorResponse(500, "Internal server error: " + std::string(e.what()));
        }
    }

    network::HttpResponse handleStatsSummaryRequest(const network::HttpRequest& request) {
        (void)request;  // Suppress unused parameter warning
        try {
            if (!stats_collector_ || !stats_collector_->isRunning()) {
                return createErrorResponse(503, "Statistics collector not available");
            }

            auto all_stats = stats_collector_->getAllStats();

            json response_json;
            response_json["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count();
            response_json["total_symbols"] = all_stats.size();
            response_json["total_trades_processed"] = stats_collector_->getTotalTradesProcessed();
            response_json["total_trades_dropped"] = stats_collector_->getTotalTradesDropped();
            response_json["queue_size"] = stats_collector_->getQueueSize();

            // Market-wide aggregates
            double total_volume = 0.0;
            double total_dollar_volume = 0.0;
            int total_trades = 0;
            double max_price = 0.0;
            double min_price = std::numeric_limits<double>::max();

            for (const auto& [symbol, stats] : all_stats) {
                // Use 1m timeframe for summary (most recent data)
                auto it = stats.timeframes.find("1m");
                if (it != stats.timeframes.end() && !it->second.isEmpty()) {
                    const auto& bucket = it->second;
                    total_volume += bucket.volume;
                    total_dollar_volume += bucket.dollar_volume;
                    total_trades += bucket.trade_count;

                    max_price = std::max(max_price, bucket.high);
                    if (bucket.low > 0) {
                        min_price = std::min(min_price, bucket.low);
                    }
                }
            }

            response_json["market_summary"] = {
                {"total_volume", total_volume},
                {"total_dollar_volume", total_dollar_volume},
                {"total_trades", total_trades},
                {"price_range",
                 {{"min", min_price == std::numeric_limits<double>::max() ? 0.0 : min_price},
                  {"max", max_price}}}};

            network::HttpResponse response;
            response.status_code = 200;
            response.body = response_json.dump();
            response.headers["Content-Type"] = "application/json";
            return response;

        } catch (const std::exception& e) {
            return createErrorResponse(500, "Internal server error: " + std::string(e.what()));
        }
    }

    network::HttpResponse handleLeaderboardRequest(const network::HttpRequest& request) {
        (void)request;  // Suppress unused parameter warning
        try {
            auto& all_users = matching_engine_->getAllUsers();

            if (all_users.empty()) {
                json response_json;
                response_json["timestamp"] =
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
                response_json["total_users"] = 0;
                response_json["leaderboard"] = json::array();

                network::HttpResponse response;
                response.status_code = 200;
                response.body = response_json.dump();
                response.headers["Content-Type"] = "application/json";
                return response;
            }

            // Calculate net worth for each user
            std::vector<std::pair<std::string, double>> user_networth_pairs;

            for (const auto& [user_id, user_ptr] : all_users) {
                if (!user_ptr)
                    continue;

                double net_worth = user_ptr->getCashBalance();

                // Add portfolio value
                const auto& positions = user_ptr->getAllPositions();
                for (const auto& [symbol, position] : positions) {
                    if (position.quantity <= 0.0)
                        continue;

                    // Get current market price for the symbol
                    auto orderbook = matching_engine_->getOrderBook(symbol);
                    double market_price = 0.0;

                    if (orderbook) {
                        double best_bid = orderbook->getBestBid();
                        double best_ask = orderbook->getBestAsk();

                        // Use mid-price if both bid and ask are available, otherwise use available
                        // one
                        if (best_bid > 0.0 && best_ask > 0.0) {
                            market_price = (best_bid + best_ask) / 2.0;
                        } else if (best_bid > 0.0) {
                            market_price = best_bid;
                        } else if (best_ask > 0.0) {
                            market_price = best_ask;
                        } else {
                            // No market data available, use cost basis (average price)
                            market_price = position.average_price;
                        }
                    } else {
                        // No order book exists, use cost basis
                        market_price = position.average_price;
                    }

                    net_worth += position.quantity * market_price;
                }

                user_networth_pairs.emplace_back(user_id, net_worth);
            }

            // Sort by net worth descending
            std::sort(user_networth_pairs.begin(), user_networth_pairs.end(),
                      [](const auto& a, const auto& b) {
                          return a.second > b.second;  // Higher net worth first
                      });

            // Build response JSON
            json response_json;
            response_json["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count();
            response_json["total_users"] = user_networth_pairs.size();

            json leaderboard = json::array();
            int rank = 1;
            for (const auto& [user_id, net_worth] : user_networth_pairs) {
                auto user_ptr = all_users.at(user_id);
                if (!user_ptr)
                    continue;

                json user_entry;
                user_entry["rank"] = rank++;
                user_entry["user_id"] = user_id;
                user_entry["net_worth"] = net_worth;
                user_entry["cash_balance"] = user_ptr->getCashBalance();
                user_entry["realized_pnl"] = user_ptr->getRealizedPnl();

                // Calculate portfolio value
                double portfolio_value = net_worth - user_ptr->getCashBalance();
                user_entry["portfolio_value"] = portfolio_value;

                // Add position details
                const auto& positions = user_ptr->getAllPositions();
                json positions_json = json::array();
                for (const auto& [symbol, position] : positions) {
                    if (position.quantity <= 0.0)
                        continue;

                    // Get current market price
                    auto orderbook = matching_engine_->getOrderBook(symbol);
                    double market_price = position.average_price;  // fallback

                    if (orderbook) {
                        double best_bid = orderbook->getBestBid();
                        double best_ask = orderbook->getBestAsk();

                        if (best_bid > 0.0 && best_ask > 0.0) {
                            market_price = (best_bid + best_ask) / 2.0;
                        } else if (best_bid > 0.0) {
                            market_price = best_bid;
                        } else if (best_ask > 0.0) {
                            market_price = best_ask;
                        }
                    }

                    json pos_json;
                    pos_json["symbol"] = symbol;
                    pos_json["quantity"] = position.quantity;
                    pos_json["average_price"] = position.average_price;
                    pos_json["current_price"] = market_price;
                    pos_json["market_value"] = position.quantity * market_price;
                    pos_json["unrealized_pnl"] =
                        (market_price - position.average_price) * position.quantity;

                    positions_json.push_back(pos_json);
                }
                user_entry["positions"] = positions_json;

                leaderboard.push_back(user_entry);
            }

            response_json["leaderboard"] = leaderboard;

            network::HttpResponse response;
            response.status_code = 200;
            response.body = response_json.dump();
            response.headers["Content-Type"] = "application/json";
            return response;

        } catch (const std::exception& e) {
            return createErrorResponse(500, "Internal server error: " + std::string(e.what()));
        }
    }

    void handleTrade(const core::Trade& trade) {
        trade_logger_->logTrade(trade);

        // Submit trade to statistics collector
        if (stats_collector_ && stats_collector_->isRunning()) {
            stats_collector_->submitTrade(trade);
        }

        // Execute the trade
        execution::ExecutionResult result = executor_->execute(trade);

        // Create and send confirmation
        auto confirmation = trade_logger_->createConfirmation(trade);
        trade_logger_->sendConfirmation(confirmation);
    }

    void handleExecution(const execution::ExecutionResult& result) {
        trade_logger_->logExecution(result);
    }

    network::HttpResponse createErrorResponse(int status_code, const std::string& message) {
        network::HttpResponse response;
        response.status_code = status_code;
        response.body = json{{"error", message}}.dump();
        response.headers["Content-Type"] = "application/json";
        return response;
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
    std::unique_ptr<statistics::StatisticsCollector> stats_collector_;
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

    // Start the global logger's async thread
    try {
        g_logger->start();
    } catch (const std::runtime_error& e) {
        std::cerr << "Failed to start global logger: " << e.what() << std::endl;
        return 1;
    }

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

    // Stop the global logger's async thread (flush remaining messages)
    g_logger->stop();

    return 0;
}