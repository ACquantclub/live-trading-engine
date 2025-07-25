#pragma once

#include <cstdint>
#include <string>

namespace trading::core {

enum class OrderType : std::uint8_t { LIMIT, MARKET, STOP };

enum class OrderSide : std::uint8_t { BUY, SELL };

enum class OrderStatus : std::uint8_t { PENDING, PARTIALLY_FILLED, FILLED, REJECTED, CANCELLED };

class Order {
  public:
    Order();
    Order(const std::string& id, const std::string& symbol, OrderType type, OrderSide side,
          double quantity, double price = 0.0);
    ~Order() = default;

    // Getters
    [[nodiscard]] const std::string& getId() const noexcept;
    [[nodiscard]] const std::string& getSymbol() const noexcept;
    [[nodiscard]] constexpr OrderType getType() const noexcept;
    [[nodiscard]] constexpr OrderSide getSide() const noexcept;
    [[nodiscard]] constexpr double getQuantity() const noexcept;
    [[nodiscard]] constexpr double getPrice() const noexcept;
    [[nodiscard]] constexpr double getFilledQuantity() const noexcept;
    [[nodiscard]] constexpr OrderStatus getStatus() const noexcept;

    // Setters
    void setStatus(OrderStatus status) noexcept;
    void addFill(double quantity) noexcept;

    // C++23 formatting support
    [[nodiscard]] std::string toString() const;

  private:
    std::string id_;
    std::string symbol_;
    OrderType type_;
    OrderSide side_;
    double quantity_;
    double price_;
    double filled_quantity_;
    OrderStatus status_;
};

}  // namespace trading::core

// Helper functions for enum to string conversion
inline const char* orderTypeToString(trading::core::OrderType type) {
    switch (type) {
        case trading::core::OrderType::LIMIT:
            return "LIMIT";
        case trading::core::OrderType::MARKET:
            return "MARKET";
        case trading::core::OrderType::STOP:
            return "STOP";
    }
    return "UNKNOWN";
}

inline const char* orderSideToString(trading::core::OrderSide side) {
    return (side == trading::core::OrderSide::BUY) ? "BUY" : "SELL";
}