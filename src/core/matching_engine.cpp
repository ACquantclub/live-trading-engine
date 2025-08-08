#include "trading/core/matching_engine.hpp"
#include <chrono>

namespace trading {
namespace core {

MatchingEngine::MatchingEngine() : total_trades_(0), total_volume_(0.0), next_trade_id_(1) {
}

void MatchingEngine::addOrderBook(const std::string& symbol, std::shared_ptr<OrderBook> orderbook) {
    orderbooks_[symbol] = orderbook;
}

std::vector<Trade> MatchingEngine::matchOrder(std::shared_ptr<Order> order, OrderBook& orderbook) {
    // Match market orders
    if (order->getType() == OrderType::MARKET) {
        return matchMarketOrder(order, orderbook);
    }

    else if (order->getType() == OrderType::LIMIT) {
        return matchLimitOrder(order, orderbook);
    }

    return std::vector<Trade>();
}

void MatchingEngine::setTradeCallback(TradeCallback callback) {
    trade_callback_ = callback;
}

uint64_t MatchingEngine::getTotalTrades() const {
    return total_trades_;
}

double MatchingEngine::getTotalVolume() const {
    return total_volume_;
}

std::vector<Trade> MatchingEngine::matchMarketOrder(std::shared_ptr<Order> order,
                                                    OrderBook& orderbook) {
    // Find orders of the opposite side
    auto trades_opposite_side =
        (order->getSide() == OrderSide::BUY) ? orderbook.getSellOrders() : orderbook.getBuyOrders();

    // Match to order with the best price
    auto best_price_order =
        (order->getSide() == OrderSide::BUY) ? orderbook.getBestAsk() : orderbook.getBestBid();

    // Create trades until the order is fully matched or no more opposite orders are available
    std::vector<Trade> trades;
    double remaining_quantity = order->getQuantity();

    for (const auto& opposite_order : trades_opposite_side) {
        if (remaining_quantity <= 0) {
            break;  // Order is fully matched
        }

        // Check if the opposite order can be matched
        if ((order->getSide() == OrderSide::BUY &&
             opposite_order->getPrice() <= best_price_order) ||
            (order->getSide() == OrderSide::SELL &&
             opposite_order->getPrice() >= best_price_order)) {
            double trade_quantity = std::min(remaining_quantity, opposite_order->getQuantity());
            Trade trade = createTrade(order, opposite_order, trade_quantity, best_price_order);
            trades.push_back(trade);
            remaining_quantity -= trade_quantity;

            // Update the opposite order's quantity
            opposite_order->setQuantity(opposite_order->getQuantity() - trade_quantity);

            // If the opposite order is fully matched, remove it from the book
            if (opposite_order->getQuantity() <= 0) {
                orderbook.removeOrder(opposite_order->getId());
            }
        }
    }

    // Update total trades and volume
    total_trades_ += trades.size();
    for (const auto& trade : trades) {
        total_volume_ += trade.quantity * trade.price;
    }

    // Return trades
    return trades;
}

std::vector<Trade> MatchingEngine::matchLimitOrder(std::shared_ptr<Order> order,
                                                   OrderBook& orderbook) {
    // Find orders of the opposite side
    auto trades_opposite_side =
        (order->getSide() == OrderSide::BUY) ? orderbook.getSellOrders() : orderbook.getBuyOrders();

    // Create trades until the order is fully matched or no more opposite orders are available
    std::vector<Trade> trades;
    double remaining_quantity = order->getQuantity();

    for (const auto& opposite_order : trades_opposite_side) {
        if (remaining_quantity <= 0) {
            break;  // Order is fully matched
        }

        // Check if the opposite order can be matched
        if ((order->getSide() == OrderSide::BUY &&
             opposite_order->getPrice() <= order->getPrice()) ||
            (order->getSide() == OrderSide::SELL &&
             opposite_order->getPrice() >= order->getPrice())) {
            double trade_quantity = std::min(remaining_quantity, opposite_order->getQuantity());
            Trade trade = createTrade(order, opposite_order, trade_quantity, order->getPrice());
            trades.push_back(trade);
            remaining_quantity -= trade_quantity;

            // Update the opposite order's quantity
            opposite_order->setQuantity(opposite_order->getQuantity() - trade_quantity);

            // If the opposite order is fully matched, remove it from the book
            if (opposite_order->getQuantity() <= 0) {
                orderbook.removeOrder(opposite_order->getId());
            }
        }
    }

    // Update total trades and volume
    total_trades_ += trades.size();
    for (const auto& trade : trades) {
        total_volume_ += trade.quantity * trade.price;
    }

    // Return trades
    return trades;
}

Trade MatchingEngine::createTrade(std::shared_ptr<Order> buy_order,
                                  std::shared_ptr<Order> sell_order, double quantity,
                                  double price) {
    Trade trade;
    trade.trade_id = std::to_string(next_trade_id_++);
    trade.buy_order_id = buy_order->getId();
    trade.sell_order_id = sell_order->getId();
    trade.symbol = buy_order->getSymbol();
    trade.quantity = quantity;
    trade.price = price;
    trade.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    return trade;
}

std::shared_ptr<OrderBook> MatchingEngine::getOrderBook(const std::string& symbol) {
    auto it = orderbooks_.find(symbol);
    if (it != orderbooks_.end()) {
        return it->second;
    }
    return nullptr;
}

}  // namespace core
}  // namespace trading