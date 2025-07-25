#pragma once

#include <map>
#include <memory>
#include <vector>
#include "order.hpp"

namespace trading {
namespace core {

class OrderBook {
  public:
    OrderBook(const std::string& symbol);
    ~OrderBook() = default;

    // Order management
    bool addOrder(std::shared_ptr<Order> order);
    bool removeOrder(const std::string& order_id);

    // Market data
    double getBestBid() const;
    double getBestAsk() const;
    double getSpread() const;

    // Query methods
    std::vector<std::shared_ptr<Order>> getBuyOrders() const;
    std::vector<std::shared_ptr<Order>> getSellOrders() const;
    std::shared_ptr<Order> findOrder(const std::string& order_id) const;

    const std::string& getSymbol() const;

  private:
    std::string symbol_;
    std::map<double, std::vector<std::shared_ptr<Order>>> buy_orders_;
    std::map<double, std::vector<std::shared_ptr<Order>>> sell_orders_;
    std::map<std::string, std::shared_ptr<Order>> order_lookup_;
};

}  // namespace core
}  // namespace trading