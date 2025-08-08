#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "order.hpp"
#include "orderbook.hpp"

namespace trading {
namespace core {

struct Trade {
    std::string trade_id;
    std::string buy_order_id;
    std::string sell_order_id;
    std::string symbol;
    double quantity;
    double price;
    uint64_t timestamp;
};

class MatchingEngine {
  public:
    using TradeCallback = std::function<void(const Trade&)>;

    MatchingEngine();
    ~MatchingEngine() = default;

    // Add order book management
    void addOrderBook(const std::string& symbol, std::shared_ptr<OrderBook> orderbook);
    std::shared_ptr<OrderBook> getOrderBook(const std::string& symbol);

    // Matching logic
    std::vector<Trade> matchOrder(std::shared_ptr<Order> order, OrderBook& orderbook);

    // Event handling
    void setTradeCallback(TradeCallback callback);

    // Statistics
    uint64_t getTotalTrades() const;
    double getTotalVolume() const;

  private:
    TradeCallback trade_callback_;
    uint64_t total_trades_;
    double total_volume_;
    uint64_t next_trade_id_;
    std::map<std::string, std::shared_ptr<OrderBook>> orderbooks_;

    std::vector<Trade> matchMarketOrder(std::shared_ptr<Order> order, OrderBook& orderbook);
    std::vector<Trade> matchLimitOrder(std::shared_ptr<Order> order, OrderBook& orderbook);
    Trade createTrade(std::shared_ptr<Order> buy_order, std::shared_ptr<Order> sell_order,
                      double quantity, double price);
};

}  // namespace core
}  // namespace trading