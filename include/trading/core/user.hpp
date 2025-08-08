#pragma once

#include <map>
#include <optional>
#include <string>

#include "order.hpp"

namespace trading {
namespace core {

// Represents a user's position for a single symbol
struct Position {
    std::string symbol;
    double quantity{0.0};
    double average_price{0.0};
};

// In-memory representation of a user's portfolio (cash + positions)
class User {
  public:
    explicit User(std::string user_id, double starting_cash);
    ~User() = default;

    // Identity
    [[nodiscard]] const std::string& getUserId() const noexcept {
        return user_id_;
    }

    // Balances
    [[nodiscard]] double getCashBalance() const noexcept {
        return cash_balance_;
    }
    [[nodiscard]] double getRealizedPnl() const noexcept {
        return realized_pnl_;
    }

    // Cash management
    bool depositCash(double amount) noexcept;
    bool withdrawCash(double amount) noexcept;  // returns false if insufficient funds or invalid

    // Position queries
    [[nodiscard]] std::optional<Position> getPosition(const std::string& symbol) const;
    [[nodiscard]] const std::map<std::string, Position>& getAllPositions() const noexcept {
        return symbol_to_position_;
    }

    // Apply an execution fill from this user's perspective.
    // - side: BUY means user bought (reduces cash, increases position)
    //         SELL means user sold (increases cash, reduces position)
    // - fee: optional per-fill fee to apply to cash (positive number)
    // Returns false if operation invalid (e.g., insufficient cash or position quantity)
    bool applyExecution(OrderSide side, const std::string& symbol, double executed_quantity,
                        double executed_price, double fee = 0.0);

  private:
    std::string user_id_;
    double cash_balance_;
    double realized_pnl_;
    std::map<std::string, Position> symbol_to_position_;
};

}  // namespace core
}  // namespace trading
