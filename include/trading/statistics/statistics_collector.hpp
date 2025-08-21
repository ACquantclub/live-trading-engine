#pragma once

#include "trading/core/matching_engine.hpp"
#include "trading/statistics/instrument_stats.hpp"
#include "trading/utils/concurrent_queue.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace trading {
namespace statistics {

// Event structure for trade data passed through the queue
struct TradeEvent {
    std::string symbol;
    double price;
    double quantity;
    std::chrono::system_clock::time_point timestamp;

    TradeEvent() = default;
    TradeEvent(const std::string& sym, double p, double q, std::chrono::system_clock::time_point ts)
        : symbol(sym), price(p), quantity(q), timestamp(ts) {
    }

    // Move constructor
    TradeEvent(TradeEvent&& other) noexcept
        : symbol(std::move(other.symbol)),
          price(other.price),
          quantity(other.quantity),
          timestamp(other.timestamp) {
    }

    // Move assignment
    TradeEvent& operator=(TradeEvent&& other) noexcept {
        if (this != &other) {
            symbol = std::move(other.symbol);
            price = other.price;
            quantity = other.quantity;
            timestamp = other.timestamp;
        }
        return *this;
    }

    // Delete copy semantics to ensure move-only
    TradeEvent(const TradeEvent&) = delete;
    TradeEvent& operator=(const TradeEvent&) = delete;
};

class StatisticsCollector {
  public:
    // Configuration structure
    struct Config {
        std::vector<std::string> timeframes{"1m", "1h", "1d"};
        size_t queue_capacity{10000};
        std::chrono::seconds cleanup_interval{3600};  // 1 hour
        bool enabled{true};

        Config() = default;
    };

    StatisticsCollector();
    explicit StatisticsCollector(const Config& config);
    ~StatisticsCollector();

    // Lifecycle management
    bool start();
    void stop();
    bool isRunning() const;

    // Non-blocking trade event submission (called from trading threads)
    bool submitTrade(const core::Trade& trade);
    bool submitTradeEvent(TradeEvent&& event);

    // Thread-safe statistics retrieval (called from HTTP API threads)
    std::optional<InstrumentStats> getStatsForSymbol(const std::string& symbol) const;
    std::unordered_map<std::string, InstrumentStats> getAllStats() const;

    // Statistics about the collector itself
    size_t getQueueSize() const;
    uint64_t getTotalTradesProcessed() const;
    uint64_t getTotalTradesDropped() const;

  private:
    // Configuration
    Config config_;

    // Queue for trade events (MPSC: Multiple Producers, Single Consumer)
    std::unique_ptr<utils::ConcurrentQueue<TradeEvent>> trade_queue_;

    // Statistics cache protected by shared_mutex for concurrent reads
    mutable std::shared_mutex stats_mutex_;
    std::unordered_map<std::string, InstrumentStats> instrument_stats_;

    // Background processing thread
    std::thread collector_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    // Performance metrics
    std::atomic<uint64_t> total_trades_processed_{0};
    std::atomic<uint64_t> total_trades_dropped_{0};

    // Time bucket management
    struct TimeBucket {
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        std::string timeframe;
    };

    // Private methods
    void collectorLoop();
    void processTradeEvent(const TradeEvent& event);
    void updateStatistics(const std::string& symbol, double price, double quantity,
                          const std::chrono::system_clock::time_point& timestamp);

    // Time bucket utilities
    std::vector<TimeBucket> getTimeBucketsForTrade(
        const std::chrono::system_clock::time_point& timestamp) const;
    std::string getBucketKey(const std::string& timeframe,
                             const std::chrono::system_clock::time_point& timestamp) const;

    // Volatility calculation (simplified exponential weighted moving average)
    void updateVolatility(InstrumentStats& stats, const std::string& timeframe,
                          double current_price, double previous_price);

    // Cleanup old statistics (called periodically)
    void cleanupOldStats();
    std::chrono::system_clock::time_point last_cleanup_;

    // Helper to convert core::Trade to TradeEvent
    TradeEvent tradeToEvent(const core::Trade& trade) const;
};

}  // namespace statistics
}  // namespace trading
