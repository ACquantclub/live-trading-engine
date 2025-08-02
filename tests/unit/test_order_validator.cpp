#include "trading/core/order.hpp"
#include "trading/validation/order_validator.hpp"
#include <memory>
#include <gtest/gtest.h>

namespace trading::validation {

// Test fixture for creating a common setup for tests
class OrderValidatorTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Setup a default validator
        validator.addValidSymbol("AAPL");
        validator.addValidSymbol("GOOGL");
        validator.setMinQuantity(1.0);
        validator.setMaxQuantity(1000.0);
        validator.setMinPrice(10.0);
        validator.setMaxPrice(5000.0);
        validator.setMarketOpen(true);

        // Create a standard valid limit order
        valid_limit_order =
            std::make_shared<core::Order>("ord-001", "user-001", "AAPL", core::OrderType::LIMIT,
                                          core::OrderSide::BUY, 100.0, 150.0);
    }

    OrderValidator validator;
    std::shared_ptr<core::Order> valid_limit_order;
};

// Test valid limit order
TEST_F(OrderValidatorTest, ValidLimitOrder) {
    auto result = validator.validate(valid_limit_order);
    ASSERT_TRUE(result.is_valid);
    EXPECT_EQ(result.error, ValidationError::NONE);
}

// Test invalid symbol
TEST_F(OrderValidatorTest, InvalidSymbol) {
    auto order = std::make_shared<core::Order>(
        "ord-002", "user-001", "MSFT", core::OrderType::LIMIT, core::OrderSide::BUY, 100.0, 300.0);
    auto result = validator.validate(order);
    ASSERT_FALSE(result.is_valid);
    EXPECT_EQ(result.error, ValidationError::INVALID_SYMBOL);
}

// Test quantity too low
TEST_F(OrderValidatorTest, QuantityTooLow) {
    auto order = std::make_shared<core::Order>(
        "ord-003", "user-001", "AAPL", core::OrderType::LIMIT, core::OrderSide::BUY, 0.5, 150.0);
    auto result = validator.validate(order);
    ASSERT_FALSE(result.is_valid);
    EXPECT_EQ(result.error, ValidationError::INVALID_QUANTITY);
}

// Test quantity too high
TEST_F(OrderValidatorTest, QuantityTooHigh) {
    auto order = std::make_shared<core::Order>(
        "ord-004", "user-001", "AAPL", core::OrderType::LIMIT, core::OrderSide::BUY, 1500.0, 150.0);
    auto result = validator.validate(order);
    ASSERT_FALSE(result.is_valid);
    EXPECT_EQ(result.error, ValidationError::INVALID_QUANTITY);
}

// Test price too low
TEST_F(OrderValidatorTest, PriceTooLow) {
    auto order = std::make_shared<core::Order>(
        "ord-005", "user-001", "AAPL", core::OrderType::LIMIT, core::OrderSide::BUY, 100.0, 5.0);
    auto result = validator.validate(order);
    ASSERT_FALSE(result.is_valid);
    EXPECT_EQ(result.error, ValidationError::INVALID_PRICE);
}

// Test price too high
TEST_F(OrderValidatorTest, PriceTooHigh) {
    auto order = std::make_shared<core::Order>(
        "ord-006", "user-001", "AAPL", core::OrderType::LIMIT, core::OrderSide::BUY, 100.0, 6000.0);
    auto result = validator.validate(order);
    ASSERT_FALSE(result.is_valid);
    EXPECT_EQ(result.error, ValidationError::INVALID_PRICE);
}

// Test valid market order
TEST_F(OrderValidatorTest, ValidMarketOrder) {
    auto order = std::make_shared<core::Order>(
        "ord-007", "user-001", "GOOGL", core::OrderType::MARKET, core::OrderSide::SELL, 50.0, 0.0);
    auto result = validator.validate(order);
    ASSERT_TRUE(result.is_valid);
    EXPECT_EQ(result.error, ValidationError::NONE);
}

// Test market closed
TEST_F(OrderValidatorTest, MarketClosed) {
    validator.setMarketOpen(false);
    auto result = validator.validate(valid_limit_order);
    ASSERT_FALSE(result.is_valid);
    EXPECT_EQ(result.error, ValidationError::MARKET_CLOSED);
}

}  // namespace trading::validation