#include "trading/core/user.hpp"

#include <algorithm>

namespace trading {
namespace core {

User::User(std::string user_id, double starting_cash)
    : user_id_(std::move(user_id)), cash_balance_(starting_cash), realized_pnl_(0.0) {
}

bool User::depositCash(double amount) noexcept {
    if (amount <= 0.0) {
        return false;
    }
    cash_balance_ += amount;
    return true;
}

bool User::withdrawCash(double amount) noexcept {
    if (amount <= 0.0 || amount > cash_balance_) {
        return false;
    }
    cash_balance_ -= amount;
    return true;
}

std::optional<Position> User::getPosition(const std::string& symbol) const {
    auto it = symbol_to_position_.find(symbol);
    if (it == symbol_to_position_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool User::applyExecution(OrderSide side, const std::string& symbol, double executed_quantity,
                          double executed_price, double fee) {
    if (executed_quantity <= 0.0 || executed_price < 0.0 || fee < 0.0) {
        return false;
    }

    double gross_amount = executed_quantity * executed_price;  // trade notional

    if (side == OrderSide::BUY) {
        double total_cost = gross_amount + fee;
        if (total_cost > cash_balance_) {
            return false;  // insufficient cash
        }

        // Insert or get existing position now that validation passed
        auto it = symbol_to_position_.find(symbol);
        Position* position_ptr;
        if (it == symbol_to_position_.end()) {
            auto& inserted = symbol_to_position_[symbol];
            inserted.symbol = symbol;
            position_ptr = &inserted;
        } else {
            position_ptr = &it->second;
        }
        auto& pos = *position_ptr;

        // Update weighted average price
        double new_quantity = pos.quantity + executed_quantity;
        if (new_quantity <= 0.0) {
            // Should not happen for BUY
            return false;
        }
        double previous_cost_basis = pos.average_price * pos.quantity;
        double new_cost_basis = previous_cost_basis + gross_amount;
        pos.quantity = new_quantity;
        pos.average_price = new_cost_basis / new_quantity;

        // Deduct cash
        cash_balance_ -= total_cost;
        return true;
    }

    // SELL path
    auto it = symbol_to_position_.find(symbol);
    if (it == symbol_to_position_.end()) {
        return false;  // cannot sell a non-existent position
    }
    auto& pos = it->second;
    if (executed_quantity > pos.quantity + 1e-12) {
        return false;  // cannot sell more than owned (no shorting for now)
    }

    // Realize PnL on sold quantity
    double cost_basis_of_sold = pos.average_price * executed_quantity;
    double proceeds = gross_amount - fee;
    double pnl = proceeds - cost_basis_of_sold;
    realized_pnl_ += pnl;

    // Update position quantity; average price unchanged for remaining shares
    pos.quantity -= executed_quantity;
    if (pos.quantity <= 1e-12) {
        pos.quantity = 0.0;
        pos.average_price = 0.0;  // reset when flat
    }

    // Add cash from sale
    cash_balance_ += proceeds;
    return true;
}

}  // namespace core
}  // namespace trading
