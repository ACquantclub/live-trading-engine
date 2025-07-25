#pragma once

#include <memory>
#include <string>
#include <vector>
#include "../core/order.hpp"

namespace trading {
namespace validation {

enum class ValidationError {
    NONE,
    INVALID_SYMBOL,
    INVALID_QUANTITY,
    INVALID_PRICE,
    INVALID_ORDER_TYPE,
    INSUFFICIENT_FUNDS,
    MARKET_CLOSED,
    DUPLICATE_ORDER_ID
};

struct ValidationResult {
    bool is_valid;
    ValidationError error;
    std::string error_message;
};

class OrderValidator {
  public:
    OrderValidator();
    ~OrderValidator() = default;

    // Validation methods
    ValidationResult validate(std::shared_ptr<core::Order> order) const;
    ValidationResult validateSymbol(const std::string& symbol) const;
    ValidationResult validateQuantity(double quantity) const;
    ValidationResult validatePrice(double price, core::OrderType type) const;

    // Configuration
    void addValidSymbol(const std::string& symbol);
    void removeValidSymbol(const std::string& symbol);
    void setMinQuantity(double min_quantity);
    void setMaxQuantity(double max_quantity);
    void setMinPrice(double min_price);
    void setMaxPrice(double max_price);

    // Market status
    void setMarketOpen(bool is_open);
    bool isMarketOpen() const;

  private:
    std::vector<std::string> valid_symbols_;
    double min_quantity_;
    double max_quantity_;
    double min_price_;
    double max_price_;
    bool market_open_;

    bool isValidSymbol(const std::string& symbol) const;
    bool isValidQuantity(double quantity) const;
    bool isValidPrice(double price) const;
};

}  // namespace validation
}  // namespace trading