#include "trading/core/order.hpp"
#include <sstream>

namespace trading::core {

Order::Order()
    : id_(""),
      symbol_(""),
      type_(OrderType::LIMIT),
      side_(OrderSide::BUY),
      quantity_(0.0),
      price_(0.0),
      filled_quantity_(0.0),
      status_(OrderStatus::PENDING) {
}

Order::Order(const std::string& id, const std::string& symbol, OrderType type, OrderSide side,
             double quantity, double price)
    : id_(id),
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

const std::string& Order::getSymbol() const noexcept {
    return symbol_;
}

constexpr OrderType Order::getType() const noexcept {
    return type_;
}

constexpr OrderSide Order::getSide() const noexcept {
    return side_;
}

constexpr double Order::getQuantity() const noexcept {
    return quantity_;
}

constexpr double Order::getPrice() const noexcept {
    return price_;
}

constexpr double Order::getFilledQuantity() const noexcept {
    return filled_quantity_;
}

constexpr OrderStatus Order::getStatus() const noexcept {
    return status_;
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