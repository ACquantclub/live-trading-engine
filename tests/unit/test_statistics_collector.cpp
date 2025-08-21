#include "trading/core/matching_engine.hpp"
#include "trading/statistics/instrument_stats.hpp"
#include "trading/statistics/statistics_collector.hpp"
#include <chrono>
#include <thread>
#include <gtest/gtest.h>

using namespace trading::statistics;
using namespace trading::core;

class StatisticsCollectorTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create a config with shorter cleanup interval for testing
        config_.timeframes = {"1m", "1h", "1d"};
        config_.queue_capacity = 1000;
        config_.cleanup_interval = std::chrono::seconds(1);
        config_.enabled = true;

        collector_ = std::make_unique<StatisticsCollector>(config_);
    }

    void TearDown() override {
        if (collector_ && collector_->isRunning()) {
            collector_->stop();
        }
    }

    // Helper function to create a sample trade
    Trade createTrade(const std::string& symbol, double price, double quantity) {
        Trade trade;
        trade.trade_id = "trade-" + std::to_string(trade_counter_++);
        trade.buy_order_id = "buy-" + std::to_string(trade_counter_);
        trade.sell_order_id = "sell-" + std::to_string(trade_counter_);
        trade.buy_user_id = "buyer-001";
        trade.sell_user_id = "seller-001";
        trade.symbol = symbol;
        trade.quantity = quantity;
        trade.price = price;
        trade.timestamp = static_cast<uint64_t>(
            std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
        return trade;
    }

    // Helper function to create a TradeEvent
    TradeEvent createTradeEvent(const std::string& symbol, double price, double quantity) {
        auto now = std::chrono::system_clock::now();
        return TradeEvent(symbol, price, quantity, now);
    }

    StatisticsCollector::Config config_;
    std::unique_ptr<StatisticsCollector> collector_;
    static int trade_counter_;
};

int StatisticsCollectorTest::trade_counter_ = 0;

// Basic functionality tests
TEST_F(StatisticsCollectorTest, InitialState) {
    EXPECT_FALSE(collector_->isRunning());
    EXPECT_EQ(collector_->getTotalTradesProcessed(), 0);
    EXPECT_EQ(collector_->getTotalTradesDropped(), 0);
    EXPECT_EQ(collector_->getQueueSize(), 0);
    EXPECT_TRUE(collector_->getAllStats().empty());
}

TEST_F(StatisticsCollectorTest, DefaultConstructor) {
    auto default_collector = std::make_unique<StatisticsCollector>();
    EXPECT_FALSE(default_collector->isRunning());
    EXPECT_EQ(default_collector->getTotalTradesProcessed(), 0);
    EXPECT_EQ(default_collector->getTotalTradesDropped(), 0);
}

TEST_F(StatisticsCollectorTest, DisabledCollector) {
    StatisticsCollector::Config disabled_config;
    disabled_config.enabled = false;

    auto disabled_collector = std::make_unique<StatisticsCollector>(disabled_config);

    EXPECT_FALSE(disabled_collector->start());
    EXPECT_FALSE(disabled_collector->isRunning());

    // Should not accept trades when disabled
    Trade trade = createTrade("AAPL", 150.0, 100.0);
    EXPECT_FALSE(disabled_collector->submitTrade(trade));
}

TEST_F(StatisticsCollectorTest, StartAndStop) {
    EXPECT_TRUE(collector_->start());
    EXPECT_TRUE(collector_->isRunning());

    // Starting again should return true (already running)
    EXPECT_TRUE(collector_->start());

    collector_->stop();
    EXPECT_FALSE(collector_->isRunning());

    // Stopping again should be safe
    collector_->stop();
    EXPECT_FALSE(collector_->isRunning());
}

TEST_F(StatisticsCollectorTest, SubmitTradeWhenNotRunning) {
    Trade trade = createTrade("AAPL", 150.0, 100.0);
    EXPECT_FALSE(collector_->submitTrade(trade));
    EXPECT_EQ(collector_->getTotalTradesProcessed(), 0);
}

TEST_F(StatisticsCollectorTest, SubmitTradeEventWhenNotRunning) {
    TradeEvent event = createTradeEvent("AAPL", 150.0, 100.0);
    EXPECT_FALSE(collector_->submitTradeEvent(std::move(event)));
    EXPECT_EQ(collector_->getTotalTradesProcessed(), 0);
}

TEST_F(StatisticsCollectorTest, SingleTradeProcessing) {
    ASSERT_TRUE(collector_->start());

    Trade trade = createTrade("AAPL", 150.0, 100.0);
    EXPECT_TRUE(collector_->submitTrade(trade));

    // Give some time for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify trade was processed
    EXPECT_EQ(collector_->getTotalTradesProcessed(), 1);
    EXPECT_EQ(collector_->getTotalTradesDropped(), 0);

    // Check that statistics were created
    auto stats = collector_->getStatsForSymbol("AAPL");
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->symbol, "AAPL");
    EXPECT_NEAR(stats->last_trade_price, 150.0, 1e-9);

    // Verify timeframes were updated
    for (const auto& timeframe : config_.timeframes) {
        ASSERT_TRUE(stats->timeframes.find(timeframe) != stats->timeframes.end());
        const auto& bucket = stats->timeframes.at(timeframe);

        EXPECT_FALSE(bucket.isEmpty());
        EXPECT_NEAR(bucket.open, 150.0, 1e-9);
        EXPECT_NEAR(bucket.high, 150.0, 1e-9);
        EXPECT_NEAR(bucket.low, 150.0, 1e-9);
        EXPECT_NEAR(bucket.close, 150.0, 1e-9);
        EXPECT_NEAR(bucket.volume, 100.0, 1e-9);
        EXPECT_NEAR(bucket.dollar_volume, 15000.0, 1e-9);
        EXPECT_EQ(bucket.trade_count, 1);
        EXPECT_NEAR(bucket.getVWAP(), 150.0, 1e-9);
    }
}

TEST_F(StatisticsCollectorTest, MultipleTradesProcessing) {
    ASSERT_TRUE(collector_->start());

    // Submit multiple trades for the same symbol
    std::vector<Trade> trades = {createTrade("AAPL", 150.0, 100.0),
                                 createTrade("AAPL", 155.0, 50.0),
                                 createTrade("AAPL", 145.0, 75.0)};

    for (auto& trade : trades) {
        EXPECT_TRUE(collector_->submitTrade(trade));
    }

    // Give time for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(collector_->getTotalTradesProcessed(), 3);
    EXPECT_EQ(collector_->getTotalTradesDropped(), 0);

    auto stats = collector_->getStatsForSymbol("AAPL");
    ASSERT_TRUE(stats.has_value());

    // Check 1-minute timeframe bucket
    ASSERT_TRUE(stats->timeframes.find("1m") != stats->timeframes.end());
    const auto& bucket = stats->timeframes.at("1m");

    EXPECT_NEAR(bucket.open, 150.0, 1e-9);             // First trade
    EXPECT_NEAR(bucket.high, 155.0, 1e-9);             // Highest price
    EXPECT_NEAR(bucket.low, 145.0, 1e-9);              // Lowest price
    EXPECT_NEAR(bucket.close, 145.0, 1e-9);            // Last trade
    EXPECT_NEAR(bucket.volume, 225.0, 1e-9);           // 100 + 50 + 75
    EXPECT_NEAR(bucket.dollar_volume, 33625.0, 1e-9);  // 15000 + 7750 + 10875
    EXPECT_EQ(bucket.trade_count, 3);

    // Verify VWAP calculation: 33625 / 225 = 149.444...
    EXPECT_NEAR(bucket.getVWAP(), 149.444444, 1e-5);
}

TEST_F(StatisticsCollectorTest, MultipleSymbolsProcessing) {
    ASSERT_TRUE(collector_->start());

    // Submit trades for different symbols
    Trade aapl_trade = createTrade("AAPL", 150.0, 100.0);
    Trade msft_trade = createTrade("MSFT", 300.0, 50.0);
    Trade googl_trade = createTrade("GOOGL", 2500.0, 25.0);

    EXPECT_TRUE(collector_->submitTrade(aapl_trade));
    EXPECT_TRUE(collector_->submitTrade(msft_trade));
    EXPECT_TRUE(collector_->submitTrade(googl_trade));

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    EXPECT_EQ(collector_->getTotalTradesProcessed(), 3);

    // Verify all symbols have statistics
    auto all_stats = collector_->getAllStats();
    EXPECT_EQ(all_stats.size(), 3);

    ASSERT_TRUE(all_stats.find("AAPL") != all_stats.end());
    ASSERT_TRUE(all_stats.find("MSFT") != all_stats.end());
    ASSERT_TRUE(all_stats.find("GOOGL") != all_stats.end());

    // Verify individual statistics
    EXPECT_NEAR(all_stats["AAPL"].last_trade_price, 150.0, 1e-9);
    EXPECT_NEAR(all_stats["MSFT"].last_trade_price, 300.0, 1e-9);
    EXPECT_NEAR(all_stats["GOOGL"].last_trade_price, 2500.0, 1e-9);
}

TEST_F(StatisticsCollectorTest, TradeEventDirectSubmission) {
    ASSERT_TRUE(collector_->start());

    TradeEvent event = createTradeEvent("TSLA", 800.0, 200.0);
    EXPECT_TRUE(collector_->submitTradeEvent(std::move(event)));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(collector_->getTotalTradesProcessed(), 1);

    auto stats = collector_->getStatsForSymbol("TSLA");
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->symbol, "TSLA");
    EXPECT_NEAR(stats->last_trade_price, 800.0, 1e-9);
}

TEST_F(StatisticsCollectorTest, GetStatsForNonExistentSymbol) {
    ASSERT_TRUE(collector_->start());

    auto stats = collector_->getStatsForSymbol("NONEXISTENT");
    EXPECT_FALSE(stats.has_value());
}

TEST_F(StatisticsCollectorTest, QueueCapacityHandling) {
    // Create collector with very small queue
    StatisticsCollector::Config small_config;
    small_config.queue_capacity = 2;
    small_config.enabled = true;

    auto small_collector = std::make_unique<StatisticsCollector>(small_config);
    ASSERT_TRUE(small_collector->start());

    // Fill up the queue
    Trade trade1 = createTrade("AAPL", 150.0, 100.0);
    Trade trade2 = createTrade("AAPL", 151.0, 100.0);
    Trade trade3 = createTrade("AAPL", 152.0, 100.0);

    EXPECT_TRUE(small_collector->submitTrade(trade1));
    EXPECT_TRUE(small_collector->submitTrade(trade2));

    // Give processing thread time to consume some trades
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should still be able to submit more trades
    EXPECT_TRUE(small_collector->submitTrade(trade3));

    small_collector->stop();
}

TEST_F(StatisticsCollectorTest, OHLCVBucketFunctionality) {
    // Test empty bucket
    OHLCVBucket empty_bucket;
    EXPECT_TRUE(empty_bucket.isEmpty());
    EXPECT_NEAR(empty_bucket.getVWAP(), 0.0, 1e-9);

    // Test bucket with constructor
    OHLCVBucket bucket(100.0, 50.0);
    EXPECT_FALSE(bucket.isEmpty());
    EXPECT_NEAR(bucket.open, 100.0, 1e-9);
    EXPECT_NEAR(bucket.high, 100.0, 1e-9);
    EXPECT_NEAR(bucket.low, 100.0, 1e-9);
    EXPECT_NEAR(bucket.close, 100.0, 1e-9);
    EXPECT_NEAR(bucket.volume, 50.0, 1e-9);
    EXPECT_NEAR(bucket.dollar_volume, 5000.0, 1e-9);
    EXPECT_EQ(bucket.trade_count, 1);
    EXPECT_NEAR(bucket.getVWAP(), 100.0, 1e-9);

    // Update with new trade
    bucket.updateWithTrade(105.0, 25.0);
    EXPECT_NEAR(bucket.open, 100.0, 1e-9);            // Unchanged
    EXPECT_NEAR(bucket.high, 105.0, 1e-9);            // Updated
    EXPECT_NEAR(bucket.low, 100.0, 1e-9);             // Unchanged
    EXPECT_NEAR(bucket.close, 105.0, 1e-9);           // Updated
    EXPECT_NEAR(bucket.volume, 75.0, 1e-9);           // 50 + 25
    EXPECT_NEAR(bucket.dollar_volume, 7625.0, 1e-9);  // 5000 + 2625
    EXPECT_EQ(bucket.trade_count, 2);
    EXPECT_NEAR(bucket.getVWAP(), 101.666667, 1e-5);  // 7625 / 75

    // Update with lower price
    bucket.updateWithTrade(95.0, 100.0);
    EXPECT_NEAR(bucket.low, 95.0, 1e-9);    // Updated
    EXPECT_NEAR(bucket.close, 95.0, 1e-9);  // Updated to last trade
    EXPECT_EQ(bucket.trade_count, 3);
}

TEST_F(StatisticsCollectorTest, InstrumentStatsJsonSerialization) {
    InstrumentStats stats("AAPL");
    stats.updateWithTrade(150.0, 100.0, "1m");
    stats.updateWithTrade(155.0, 50.0, "1m");

    auto json = stats.toJson();

    EXPECT_EQ(json["symbol"], "AAPL");
    EXPECT_NEAR(json["last_trade_price"], 155.0, 1e-9);
    ASSERT_TRUE(json.contains("timeframes"));
    ASSERT_TRUE(json["timeframes"].contains("1m"));

    auto timeframe_json = json["timeframes"]["1m"];
    EXPECT_NEAR(timeframe_json["open"], 150.0, 1e-9);
    EXPECT_NEAR(timeframe_json["close"], 155.0, 1e-9);
    EXPECT_NEAR(timeframe_json["volume"], 150.0, 1e-9);
    EXPECT_EQ(timeframe_json["trade_count"], 2);
}

TEST_F(StatisticsCollectorTest, OHLCVBucketJsonSerialization) {
    OHLCVBucket bucket(100.0, 50.0);
    bucket.updateWithTrade(110.0, 25.0);
    bucket.simple_return = 0.05;
    bucket.volatility = 0.15;

    auto json = bucket.toJson();

    EXPECT_NEAR(json["open"], 100.0, 1e-9);
    EXPECT_NEAR(json["high"], 110.0, 1e-9);
    EXPECT_NEAR(json["low"], 100.0, 1e-9);
    EXPECT_NEAR(json["close"], 110.0, 1e-9);
    EXPECT_NEAR(json["volume"], 75.0, 1e-9);
    EXPECT_NEAR(json["simple_return"], 0.05, 1e-9);
    EXPECT_NEAR(json["volatility"], 0.15, 1e-9);
    EXPECT_EQ(json["trade_count"], 2);
    EXPECT_TRUE(json.contains("vwap"));
}

TEST_F(StatisticsCollectorTest, ReturnsAndVolatilityCalculation) {
    ASSERT_TRUE(collector_->start());

    // Submit trades with varying prices to test volatility
    std::vector<Trade> trades = {createTrade("AAPL", 100.0, 50.0), createTrade("AAPL", 105.0, 50.0),
                                 createTrade("AAPL", 98.0, 50.0), createTrade("AAPL", 102.0, 50.0)};

    for (auto& trade : trades) {
        EXPECT_TRUE(collector_->submitTrade(trade));
        // Small delay between trades to ensure proper sequencing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto stats = collector_->getStatsForSymbol("AAPL");
    ASSERT_TRUE(stats.has_value());

    // Check that volatility was calculated (should be > 0 due to price changes)
    const auto& bucket = stats->timeframes.at("1m");
    EXPECT_GT(bucket.volatility, 0.0);
}

TEST_F(StatisticsCollectorTest, ThreadSafetyTest) {
    ASSERT_TRUE(collector_->start());

    const int num_threads = 4;
    const int trades_per_thread = 25;
    std::vector<std::thread> threads;

    // Start multiple threads submitting trades
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t, trades_per_thread]() {
            for (int i = 0; i < trades_per_thread; ++i) {
                Trade trade = createTrade("THREAD_TEST", 100.0 + t, 10.0);
                collector_->submitTrade(trade);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    // Join all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Wait for processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Verify all trades were processed
    EXPECT_EQ(collector_->getTotalTradesProcessed(), num_threads * trades_per_thread);

    auto stats = collector_->getStatsForSymbol("THREAD_TEST");
    ASSERT_TRUE(stats.has_value());

    const auto& bucket = stats->timeframes.at("1m");
    EXPECT_EQ(bucket.trade_count, num_threads * trades_per_thread);
}

TEST_F(StatisticsCollectorTest, StopWithPendingTrades) {
    ASSERT_TRUE(collector_->start());

    // Submit many trades quickly
    for (int i = 0; i < 50; ++i) {
        Trade trade = createTrade("STOP_TEST", 100.0 + i, 10.0);
        collector_->submitTrade(trade);
    }

    // Stop immediately without waiting
    collector_->stop();

    // Should have processed some trades during shutdown
    EXPECT_GT(collector_->getTotalTradesProcessed(), 0);
}