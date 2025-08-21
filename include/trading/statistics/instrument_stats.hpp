#pragma once

#include <map>
#include <string>
#include "../../../apps/json.hpp"

using json = nlohmann::json;

namespace trading {
namespace statistics {

struct OHLCVBucket {
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    double dollar_volume = 0.0;
    double simple_return = 0.0;
    double volatility = 0.0;
    int trade_count = 0;

    // Default constructor
    OHLCVBucket() = default;

    // Constructor for first trade in bucket
    explicit OHLCVBucket(double price, double vol)
        : open(price),
          high(price),
          low(price),
          close(price),
          volume(vol),
          dollar_volume(price * vol),
          trade_count(1) {
    }

    // Update bucket with new trade
    void updateWithTrade(double price, double vol) {
        if (trade_count == 0) {
            // First trade in bucket
            open = high = low = close = price;
            volume = vol;
            dollar_volume = price * vol;
            trade_count = 1;
        } else {
            // Update existing bucket
            high = std::max(high, price);
            low = std::min(low, price);
            close = price;
            volume += vol;
            dollar_volume += (price * vol);
            trade_count++;
        }
    }

    // Check if bucket has any data
    bool isEmpty() const {
        return trade_count == 0;
    }

    // Calculate VWAP (Volume Weighted Average Price)
    double getVWAP() const {
        return (volume > 0.0) ? (dollar_volume / volume) : 0.0;
    }

    // JSON Serialization
    json toJson() const {
        return json{{"open", open},
                    {"high", high},
                    {"low", low},
                    {"close", close},
                    {"volume", volume},
                    {"dollar_volume", dollar_volume},
                    {"simple_return", simple_return},
                    {"volatility", volatility},
                    {"trade_count", trade_count},
                    {"vwap", getVWAP()}};
    }
};

struct InstrumentStats {
    std::string symbol;
    double last_trade_price = 0.0;
    std::map<std::string, OHLCVBucket> timeframes;

    // Default constructor
    InstrumentStats() = default;

    // Constructor with symbol
    explicit InstrumentStats(const std::string& sym) : symbol(sym) {
    }

    // Update statistics with new trade
    void updateWithTrade(double price, double volume, const std::string& timeframe) {
        last_trade_price = price;
        timeframes[timeframe].updateWithTrade(price, volume);
    }

    // Calculate simple return for a timeframe
    void calculateReturns(const std::string& timeframe, double previous_close) {
        auto& bucket = timeframes[timeframe];
        if (!bucket.isEmpty() && previous_close > 0.0) {
            bucket.simple_return = (bucket.close - previous_close) / previous_close;
        }
    }

    // Set volatility for a timeframe
    void setVolatility(const std::string& timeframe, double vol) {
        timeframes[timeframe].volatility = vol;
    }

    // JSON Serialization
    json toJson() const {
        json result;
        result["symbol"] = symbol;
        result["last_trade_price"] = last_trade_price;

        json timeframes_json;
        for (const auto& [tf, bucket] : timeframes) {
            timeframes_json[tf] = bucket.toJson();
        }
        result["timeframes"] = timeframes_json;

        return result;
    }
};

}  // namespace statistics
}  // namespace trading