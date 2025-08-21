#include "trading/core/orderbook.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "nlohmann/json.hpp"
using json = nlohmann::json;

namespace trading {
namespace core {

OrderBook::OrderBook(const std::string& symbol) : symbol_(symbol) {
}

bool OrderBook::addOrder(std::shared_ptr<Order> order) {
    if (!order) {
        return false;
    }

    // Check if the order symbol matches the order book symbol
    if (order->getSymbol() != symbol_) {
        return false;
    }

    // Store order based on side
    if (order->getSide() == OrderSide::BUY) {
        buy_orders_[order->getPrice()].push_back(order);
    } else if (order->getSide() == OrderSide::SELL) {
        sell_orders_[order->getPrice()].push_back(order);
    } else {
        return false;
    }

    // Set order status
    order->setStatus(OrderStatus::PENDING);
    return true;
}

bool OrderBook::removeOrder(const std::string& order_id) {
    (void)order_id;  // Suppress unused parameter warning
    // TODO: Implement order removal logic
    return false;
}

double OrderBook::getBestBid() const {
    if (buy_orders_.empty()) {
        return 0.0;  // No buy orders available
    }

    // Get the highest price from the buy orders
    return buy_orders_.begin()->first;
}

double OrderBook::getBestAsk() const {
    if (sell_orders_.empty()) {
        return 0.0;  // No sell orders available
    }

    // Get the lowest price from the sell orders
    return sell_orders_.begin()->first;
}

double OrderBook::getSpread() const {
    return getBestAsk() - getBestBid();
}

std::vector<std::shared_ptr<Order>> OrderBook::getBuyOrders() const {
    std::vector<std::shared_ptr<Order>> orders;
    for (const auto& [price, order_list] : buy_orders_) {
        orders.insert(orders.end(), order_list.begin(), order_list.end());
    }
    return orders;
}

std::vector<std::shared_ptr<Order>> OrderBook::getSellOrders() const {
    std::vector<std::shared_ptr<Order>> orders;
    for (const auto& [price, order_list] : sell_orders_) {
        orders.insert(orders.end(), order_list.begin(), order_list.end());
    }
    return orders;
}

std::shared_ptr<Order> OrderBook::findOrder(const std::string& order_id) const {
    (void)order_id;  // Suppress unused parameter warning
    // TODO: Implement order lookup
    return nullptr;
}

const std::string& OrderBook::getSymbol() const {
    return symbol_;
}

std::string OrderBook::toJSON() const {
    json orderbook_json;
    orderbook_json["symbol"] = symbol_;

    // Serialize bids (buy orders, highest price first)
    json bids = json::array();
    for (const auto& [price, orders] : buy_orders_) {
        if (!orders.empty()) {
            double total_quantity = 0.0;
            for (const auto& order : orders) {
                total_quantity += order->getQuantity();
            }
            bids.push_back({{"price", price}, {"quantity", total_quantity}});
        }
    }
    orderbook_json["bids"] = bids;

    // Serialize asks (sell orders, lowest price first)
    json asks = json::array();
    for (const auto& [price, orders] : sell_orders_) {
        if (!orders.empty()) {
            double total_quantity = 0.0;
            for (const auto& order : orders) {
                total_quantity += order->getQuantity();
            }
            asks.push_back({{"price", price}, {"quantity", total_quantity}});
        }
    }
    orderbook_json["asks"] = asks;

    // Add market data
    orderbook_json["best_bid"] = getBestBid();
    orderbook_json["best_ask"] = getBestAsk();
    orderbook_json["spread"] = getSpread();

    return orderbook_json.dump();
}

std::map<double, std::vector<std::shared_ptr<Order>>, std::greater<double>>&
OrderBook::getBuyOrdersMap() {
    return buy_orders_;
}

std::map<double, std::vector<std::shared_ptr<Order>>>& OrderBook::getSellOrdersMap() {
    return sell_orders_;
}

}  // namespace core
}  // namespace trading