#include "trading/validation/order_validator.hpp"
#include <algorithm>

namespace trading {
namespace validation {

OrderValidator::OrderValidator()
    : min_quantity_(0.01),
      max_quantity_(1000000.0),
      min_price_(0.01),
      max_price_(1000000.0),
      market_open_(true) {
}

ValidationResult OrderValidator::validate(std::shared_ptr<core::Order> order) const {
    (void)order;  // Suppress unused parameter warning
    ValidationResult result;
    result.is_valid = true;
    result.error = ValidationError::NONE;
    result.error_message = "";

    // Check if market is open
    if (!isMarketOpen()) {
        result.is_valid = false;
        result.error = ValidationError::MARKET_CLOSED;
        result.error_message = "Market is closed";
        return result;
    }

    // Check if the symbol is valid
    result = validateSymbol(order->getSymbol());
    if (!result.is_valid) {
        return result;
    }

    // Check if the quantity is valid
    result = validateQuantity(order->getQuantity());
    if (!result.is_valid) {
        return result;
    }

    // Check if the price is valid
    result = validatePrice(order->getPrice(), order->getType());
    if (!result.is_valid) {
        return result;
    }

    return result;
}

ValidationResult OrderValidator::validateSymbol(const std::string& symbol) const {
    ValidationResult result;
    result.is_valid = isValidSymbol(symbol);
    result.error = result.is_valid ? ValidationError::NONE : ValidationError::INVALID_SYMBOL;
    result.error_message = result.is_valid ? "" : "Invalid symbol: " + symbol;
    return result;
}

ValidationResult OrderValidator::validateQuantity(double quantity) const {
    ValidationResult result;
    result.is_valid = isValidQuantity(quantity);
    result.error = result.is_valid ? ValidationError::NONE : ValidationError::INVALID_QUANTITY;
    result.error_message = result.is_valid ? "" : "Invalid quantity: " + std::to_string(quantity);
    return result;
}

ValidationResult OrderValidator::validatePrice(double price, core::OrderType type) const {
    ValidationResult result;
    result.is_valid = (type == core::OrderType::MARKET) || isValidPrice(price);
    result.error = result.is_valid ? ValidationError::NONE : ValidationError::INVALID_PRICE;
    result.error_message = result.is_valid ? "" : "Invalid price: " + std::to_string(price);
    return result;
}

void OrderValidator::addValidSymbol(const std::string& symbol) {
    if (std::find(valid_symbols_.begin(), valid_symbols_.end(), symbol) == valid_symbols_.end()) {
        valid_symbols_.push_back(symbol);
    }
}

void OrderValidator::removeValidSymbol(const std::string& symbol) {
    valid_symbols_.erase(std::remove(valid_symbols_.begin(), valid_symbols_.end(), symbol),
                         valid_symbols_.end());
}

void OrderValidator::setMinQuantity(double min_quantity) {
    min_quantity_ = min_quantity;
}

void OrderValidator::setMaxQuantity(double max_quantity) {
    max_quantity_ = max_quantity;
}

void OrderValidator::setMinPrice(double min_price) {
    min_price_ = min_price;
}

void OrderValidator::setMaxPrice(double max_price) {
    max_price_ = max_price;
}

void OrderValidator::setMarketOpen(bool is_open) {
    market_open_ = is_open;
}

bool OrderValidator::isMarketOpen() const {
    return market_open_;
}

bool OrderValidator::isValidSymbol(const std::string& symbol) const {
    if (valid_symbols_.empty()) {
        return !symbol.empty();
    }
    return std::find(valid_symbols_.begin(), valid_symbols_.end(), symbol) != valid_symbols_.end();
}

bool OrderValidator::isValidQuantity(double quantity) const {
    return quantity >= min_quantity_ && quantity <= max_quantity_;
}

bool OrderValidator::isValidPrice(double price) const {
    return price >= min_price_ && price <= max_price_;
}

}  // namespace validation
}  // namespace trading