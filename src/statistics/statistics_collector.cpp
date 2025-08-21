#include "trading/statistics/statistics_collector.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>

namespace trading {
namespace statistics {

StatisticsCollector::StatisticsCollector() : StatisticsCollector(Config{}) {
}

StatisticsCollector::StatisticsCollector(const Config& config)
    : config_(config), last_cleanup_(std::chrono::system_clock::now()) {
    if (config_.enabled) {
        // Initialize the concurrent queue with specified capacity
        trade_queue_ = std::make_unique<utils::ConcurrentQueue<TradeEvent>>(config_.queue_capacity);
    }
}

StatisticsCollector::~StatisticsCollector() {
    stop();
}

bool StatisticsCollector::start() {
    if (!config_.enabled) {
        return false;
    }

    if (running_.load()) {
        return true;  // Already running
    }

    stop_requested_.store(false);
    running_.store(true);

    // Start the background collector thread
    collector_thread_ = std::thread(&StatisticsCollector::collectorLoop, this);

    return true;
}

void StatisticsCollector::stop() {
    if (!running_.load()) {
        return;
    }

    // Signal the collector thread to stop
    stop_requested_.store(true);
    running_.store(false);

    // Wait for the collector thread to finish
    if (collector_thread_.joinable()) {
        collector_thread_.join();
    }
}

bool StatisticsCollector::isRunning() const {
    return running_.load();
}

bool StatisticsCollector::submitTrade(const core::Trade& trade) {
    if (!config_.enabled || !running_.load()) {
        return false;
    }

    // Convert core::Trade to TradeEvent
    TradeEvent event = tradeToEvent(trade);
    return submitTradeEvent(std::move(event));
}

bool StatisticsCollector::submitTradeEvent(TradeEvent&& event) {
    if (!config_.enabled || !running_.load()) {
        return false;
    }

    try {
        // Non-blocking enqueue - if queue is full, this will not block
        trade_queue_->enqueue(std::move(event));
        return true;
    } catch (const std::exception&) {
        total_trades_dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

std::optional<InstrumentStats> StatisticsCollector::getStatsForSymbol(
    const std::string& symbol) const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);

    auto it = instrument_stats_.find(symbol);
    if (it != instrument_stats_.end()) {
        return it->second;
    }

    return std::nullopt;
}

std::unordered_map<std::string, InstrumentStats> StatisticsCollector::getAllStats() const {
    std::shared_lock<std::shared_mutex> lock(stats_mutex_);
    return instrument_stats_;
}

size_t StatisticsCollector::getQueueSize() const {
    if (!trade_queue_) {
        return 0;
    }
    return trade_queue_->size();
}

uint64_t StatisticsCollector::getTotalTradesProcessed() const {
    return total_trades_processed_.load();
}

uint64_t StatisticsCollector::getTotalTradesDropped() const {
    return total_trades_dropped_.load();
}

void StatisticsCollector::collectorLoop() {
    TradeEvent event;
    auto last_cleanup = std::chrono::system_clock::now();

    while (!stop_requested_.load()) {
        // Try to dequeue a trade event (non-blocking)
        if (trade_queue_->try_dequeue(event)) {
            processTradeEvent(event);
            total_trades_processed_.fetch_add(1, std::memory_order_relaxed);
        } else {
            // No event available, sleep briefly to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        // Periodic cleanup of old statistics
        auto now = std::chrono::system_clock::now();
        if (now - last_cleanup >= config_.cleanup_interval) {
            cleanupOldStats();
            last_cleanup = now;
        }
    }

    // Process remaining events before shutdown
    while (trade_queue_->try_dequeue(event)) {
        processTradeEvent(event);
        total_trades_processed_.fetch_add(1, std::memory_order_relaxed);
    }
}

void StatisticsCollector::processTradeEvent(const TradeEvent& event) {
    updateStatistics(event.symbol, event.price, event.quantity, event.timestamp);
}

void StatisticsCollector::updateStatistics(const std::string& symbol, double price, double quantity,
                                           const std::chrono::system_clock::time_point& timestamp) {
    std::unique_lock<std::shared_mutex> lock(stats_mutex_);

    // Get or create instrument stats
    auto& stats = instrument_stats_[symbol];
    if (stats.symbol.empty()) {
        stats.symbol = symbol;
    }

    double previous_price = stats.last_trade_price;

    // Get all relevant time buckets for this trade
    auto buckets = getTimeBucketsForTrade(timestamp);

    // Update statistics for each timeframe
    for (const auto& bucket : buckets) {
        auto bucket_key = getBucketKey(bucket.timeframe, timestamp);

        // Store previous close price for return calculation
        double previous_close = 0.0;
        auto timeframe_it = stats.timeframes.find(bucket.timeframe);
        if (timeframe_it != stats.timeframes.end() && !timeframe_it->second.isEmpty()) {
            previous_close = timeframe_it->second.close;
        }

        // Update the timeframe bucket
        stats.updateWithTrade(price, quantity, bucket.timeframe);

        // Calculate returns if we have previous data
        if (previous_close > 0.0) {
            stats.calculateReturns(bucket.timeframe, previous_close);
        }

        // Update volatility
        if (previous_price > 0.0) {
            updateVolatility(stats, bucket.timeframe, price, previous_price);
        }
    }
}

std::vector<StatisticsCollector::TimeBucket> StatisticsCollector::getTimeBucketsForTrade(
    const std::chrono::system_clock::time_point& timestamp) const {
    std::vector<TimeBucket> buckets;

    for (const auto& timeframe : config_.timeframes) {
        TimeBucket bucket;
        bucket.timeframe = timeframe;

        // Calculate bucket boundaries based on timeframe
        auto time_t = std::chrono::system_clock::to_time_t(timestamp);
        auto* tm = std::gmtime(&time_t);

        if (timeframe == "1m") {
            tm->tm_sec = 0;
            bucket.start_time = std::chrono::system_clock::from_time_t(std::mktime(tm));
            tm->tm_min += 1;
            bucket.end_time = std::chrono::system_clock::from_time_t(std::mktime(tm));
        } else if (timeframe == "5m") {
            tm->tm_sec = 0;
            tm->tm_min = (tm->tm_min / 5) * 5;  // Round down to nearest 5-minute boundary
            bucket.start_time = std::chrono::system_clock::from_time_t(std::mktime(tm));
            tm->tm_min += 5;
            bucket.end_time = std::chrono::system_clock::from_time_t(std::mktime(tm));
        } else if (timeframe == "1h") {
            tm->tm_sec = 0;
            tm->tm_min = 0;
            bucket.start_time = std::chrono::system_clock::from_time_t(std::mktime(tm));
            tm->tm_hour += 1;
            bucket.end_time = std::chrono::system_clock::from_time_t(std::mktime(tm));
        } else if (timeframe == "1d") {
            tm->tm_sec = 0;
            tm->tm_min = 0;
            tm->tm_hour = 0;
            bucket.start_time = std::chrono::system_clock::from_time_t(std::mktime(tm));
            tm->tm_mday += 1;
            bucket.end_time = std::chrono::system_clock::from_time_t(std::mktime(tm));
        }

        buckets.push_back(bucket);
    }

    return buckets;
}

std::string StatisticsCollector::getBucketKey(
    const std::string& timeframe, const std::chrono::system_clock::time_point& timestamp) const {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto* tm = std::gmtime(&time_t);

    std::ostringstream oss;

    if (timeframe == "1m") {
        oss << std::put_time(tm, "%Y%m%d_%H%M");
    } else if (timeframe == "5m") {
        int minute_bucket = (tm->tm_min / 5) * 5;
        oss << std::put_time(tm, "%Y%m%d_%H") << std::setfill('0') << std::setw(2) << minute_bucket;
    } else if (timeframe == "1h") {
        oss << std::put_time(tm, "%Y%m%d_%H");
    } else if (timeframe == "1d") {
        oss << std::put_time(tm, "%Y%m%d");
    } else {
        oss << std::put_time(tm, "%Y%m%d_%H%M%S");
    }

    return oss.str();
}

void StatisticsCollector::updateVolatility(InstrumentStats& stats, const std::string& timeframe,
                                           double current_price, double previous_price) {
    // Simple exponential weighted moving average for volatility
    // This is a simplified implementation - in production you might want more sophisticated
    // volatility calculations

    constexpr double alpha = 0.1;  // Smoothing parameter

    double return_rate = (current_price - previous_price) / previous_price;
    double squared_return = return_rate * return_rate;

    auto& bucket = stats.timeframes[timeframe];
    if (bucket.volatility <= 0.0) {
        // First volatility calculation
        bucket.volatility = squared_return;
    } else {
        // Update using exponential weighted moving average
        bucket.volatility = alpha * squared_return + (1.0 - alpha) * bucket.volatility;
    }

    // Convert to standard deviation
    bucket.volatility = std::sqrt(bucket.volatility);
}

void StatisticsCollector::cleanupOldStats() {
    // This is a simplified cleanup - in production you might want more sophisticated logic
    // For now, we'll keep all stats but you could implement time-based cleanup here

    auto now = std::chrono::system_clock::now();
    auto cutoff_time = now - std::chrono::hours(24 * 7);  // Keep 7 days of data
    (void)cutoff_time;  // Suppress unused variable warning - would be used for actual cleanup

    std::unique_lock<std::shared_mutex> lock(stats_mutex_);

    // In a production system, you would implement logic to remove old timeframe buckets
    // based on the cutoff_time. This is left as a simple placeholder.

    // Example: Remove buckets older than cutoff_time
    for (auto& [symbol, stats] : instrument_stats_) {
        // Here you could iterate through timeframes and remove old buckets
        // This requires extending the data structures to store timestamps for each bucket
        (void)symbol;  // Suppress unused variable warning
        (void)stats;   // Suppress unused variable warning
    }
}

TradeEvent StatisticsCollector::tradeToEvent(const core::Trade& trade) const {
    // Convert Unix timestamp (uint64_t) to chrono time_point
    auto timestamp = std::chrono::system_clock::from_time_t(static_cast<time_t>(trade.timestamp));

    return TradeEvent(trade.symbol, trade.price, trade.quantity, timestamp);
}

}  // namespace statistics
}  // namespace trading