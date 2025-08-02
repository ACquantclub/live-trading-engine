#include "trading/core/order.hpp"
#include <sstream>

namespace trading::core {

Order::Order()
    : id_(""),
      userId_(""),
      symbol_(""),
      type_(OrderType::LIMIT),
      side_(OrderSide::BUY),
      quantity_(0.0),
      price_(0.0),
      filled_quantity_(0.0),
      status_(OrderStatus::PENDING) {
}

Order::Order(const std::string& id, const std::string& userId, const std::string& symbol,
             OrderType type, OrderSide side, double quantity, double price)
    : id_(id),
      userId_(userId),
      symbol_(symbol),
      type_(type),
      side_(side),
      quantity_(quantity),
      price_(price),
      filled_quantity_(0.0),
      status_(OrderStatus::PENDING) {
}

const std::string& Order::getId() const noexcept {
    return id_;
}

const std::string& Order::getUserId() const noexcept {
    return userId_;
}

const std::string& Order::getSymbol() const noexcept {
    return symbol_;
}

void Order::setStatus(OrderStatus status) noexcept {
    status_ = status;
}

void Order::addFill(double quantity) noexcept {
    filled_quantity_ += quantity;
    if (filled_quantity_ >= quantity_) {
        status_ = OrderStatus::FILLED;
    } else {
        status_ = OrderStatus::PARTIALLY_FILLED;
    }
}

std::string Order::toString() const {
    std::ostringstream oss;
    oss << "Order{id: " << id_ << ", symbol: " << symbol_ << ", type: " << static_cast<int>(type_)
        << ", side: " << static_cast<int>(side_) << ", quantity: " << quantity_
        << ", price: " << price_ << ", filled: " << filled_quantity_ << "}";
    return oss.str();
}

}  // namespace trading::core