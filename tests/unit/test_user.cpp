#include "trading/core/order.hpp"
#include "trading/core/user.hpp"
#include <gtest/gtest.h>

using namespace trading::core;

class UserTest : public ::testing::Test {
  protected:
    void SetUp() override {
        starting_cash = 10000.0;
        user = std::make_unique<User>("user-001", starting_cash);
    }

    double starting_cash;
    std::unique_ptr<User> user;
};

TEST_F(UserTest, InitialState) {
    EXPECT_EQ(user->getUserId(), "user-001");
    EXPECT_NEAR(user->getCashBalance(), starting_cash, 1e-9);
    EXPECT_NEAR(user->getRealizedPnl(), 0.0, 1e-12);
    EXPECT_TRUE(user->getAllPositions().empty());
}

TEST_F(UserTest, DepositAndWithdrawCash) {
    ASSERT_TRUE(user->depositCash(500.0));
    EXPECT_NEAR(user->getCashBalance(), starting_cash + 500.0, 1e-9);

    // Invalid deposit
    EXPECT_FALSE(user->depositCash(0.0));
    EXPECT_FALSE(user->depositCash(-10.0));
    EXPECT_NEAR(user->getCashBalance(), starting_cash + 500.0, 1e-9);

    // Valid withdrawal
    ASSERT_TRUE(user->withdrawCash(300.0));
    EXPECT_NEAR(user->getCashBalance(), starting_cash + 200.0, 1e-9);

    // Invalid withdrawal
    EXPECT_FALSE(user->withdrawCash(0.0));
    EXPECT_FALSE(user->withdrawCash(-5.0));
    EXPECT_FALSE(user->withdrawCash(starting_cash + 201.0));
    EXPECT_NEAR(user->getCashBalance(), starting_cash + 200.0, 1e-9);
}

TEST_F(UserTest, ApplyExecutionBuyCreatesAndUpdatesPosition) {
    // Buy 10 @ 100, fee 1.0
    ASSERT_TRUE(user->applyExecution(OrderSide::BUY, "AAPL", 10.0, 100.0, 1.0));
    EXPECT_NEAR(user->getCashBalance(), starting_cash - 1001.0, 1e-9);
    auto pos_opt = user->getPosition("AAPL");
    ASSERT_TRUE(pos_opt.has_value());
    EXPECT_NEAR(pos_opt->quantity, 10.0, 1e-9);
    EXPECT_NEAR(pos_opt->average_price, 100.0, 1e-12);
    EXPECT_NEAR(user->getRealizedPnl(), 0.0, 1e-12);

    // Buy additional 20 @ 110, fee 2.0
    ASSERT_TRUE(user->applyExecution(OrderSide::BUY, "AAPL", 20.0, 110.0, 2.0));
    EXPECT_NEAR(user->getCashBalance(), starting_cash - 1001.0 - 2202.0, 1e-9);
    pos_opt = user->getPosition("AAPL");
    ASSERT_TRUE(pos_opt.has_value());
    EXPECT_NEAR(pos_opt->quantity, 30.0, 1e-9);
    EXPECT_NEAR(pos_opt->average_price, (1000.0 + 2200.0) / 30.0, 1e-9);
}

TEST_F(UserTest, ApplyExecutionSellRealizesPnLAndReducesPosition) {
    // Build position: 10 @ 100 (fee 1), then 20 @ 110 (fee 2)
    ASSERT_TRUE(user->applyExecution(OrderSide::BUY, "AAPL", 10.0, 100.0, 1.0));
    ASSERT_TRUE(user->applyExecution(OrderSide::BUY, "AAPL", 20.0, 110.0, 2.0));
    const double avg_price = (1000.0 + 2200.0) / 30.0;  // 106.666...

    // Sell 5 @ 120, fee 1
    ASSERT_TRUE(user->applyExecution(OrderSide::SELL, "AAPL", 5.0, 120.0, 1.0));
    auto pos_opt = user->getPosition("AAPL");
    ASSERT_TRUE(pos_opt.has_value());
    EXPECT_NEAR(pos_opt->quantity, 25.0, 1e-9);
    EXPECT_NEAR(pos_opt->average_price, avg_price, 1e-9);  // unchanged on partial sell

    // Realized PnL: (5*120 - 1) - 5*avg
    double expected_pnl1 = (600.0 - 1.0) - 5.0 * avg_price;  // 599 - cost basis
    EXPECT_NEAR(user->getRealizedPnl(), expected_pnl1, 1e-9);

    // Sell remaining 25 @ 100, no fee
    ASSERT_TRUE(user->applyExecution(OrderSide::SELL, "AAPL", 25.0, 100.0, 0.0));
    pos_opt = user->getPosition("AAPL");
    ASSERT_TRUE(pos_opt.has_value());
    EXPECT_NEAR(pos_opt->quantity, 0.0, 1e-12);
    EXPECT_NEAR(pos_opt->average_price, 0.0, 1e-12);  // reset when flat

    // Total PnL should be -101.0 exactly with these numbers
    EXPECT_NEAR(user->getRealizedPnl(), -101.0, 1e-9);
}

TEST_F(UserTest, ApplyExecutionFailsOnInsufficientCash) {
    User low_cash_user("user-002", 100.0);
    // Buying 1 @ 100 with fee 1 exceeds 100
    EXPECT_FALSE(low_cash_user.applyExecution(OrderSide::BUY, "AAPL", 1.0, 100.0, 1.0));

    // Trade should have no effect on user's cash balance because it failed
    EXPECT_NEAR(low_cash_user.getCashBalance(), 100.0, 1e-12);

    // User should have no positions
    std::cout << "low_cash_user.getAllPositions().size(): "
              << low_cash_user.getAllPositions().size() << std::endl;
    EXPECT_TRUE(low_cash_user.getAllPositions().empty());
}

TEST_F(UserTest, ApplyExecutionFailsOnOversell) {
    ASSERT_TRUE(user->applyExecution(OrderSide::BUY, "AAPL", 5.0, 10.0, 0.0));
    // Attempt to sell more than owned
    EXPECT_FALSE(user->applyExecution(OrderSide::SELL, "AAPL", 10.0, 10.0, 0.0));
    auto pos_opt = user->getPosition("AAPL");
    ASSERT_TRUE(pos_opt.has_value());
    EXPECT_NEAR(pos_opt->quantity, 5.0, 1e-12);
}

TEST_F(UserTest, ApplyExecutionRejectsInvalidInputs) {
    // Non-positive quantity
    EXPECT_FALSE(user->applyExecution(OrderSide::BUY, "AAPL", 0.0, 100.0, 0.0));
    EXPECT_FALSE(user->applyExecution(OrderSide::BUY, "AAPL", -1.0, 100.0, 0.0));
    // Negative price or fee
    EXPECT_FALSE(user->applyExecution(OrderSide::BUY, "AAPL", 1.0, -1.0, 0.0));
    EXPECT_FALSE(user->applyExecution(OrderSide::BUY, "AAPL", 1.0, 100.0, -0.5));
}
