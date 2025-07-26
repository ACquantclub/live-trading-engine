#pragma once

#include <fstream>
#include <memory>
#include <string>
#include "../core/matching_engine.hpp"
#include "../execution/executor.hpp"
#include "log_level.hpp"

namespace trading {
namespace logging {

struct TradeConfirmation {
    std::string confirmation_id;
    std::string trade_id;
    std::string symbol;
    double quantity;
    double price;
    uint64_t timestamp;
    std::string status;
};

class TradeLogger {
  public:
    TradeLogger(const std::string& log_file_path);
    ~TradeLogger();

    // Logging methods
    virtual void logTrade(const core::Trade& trade);
    virtual void logExecution(const execution::ExecutionResult& result);
    virtual void logMessage(LogLevel level, const std::string& message);

    // Confirmation methods
    virtual TradeConfirmation createConfirmation(const core::Trade& trade);
    virtual bool sendConfirmation(const TradeConfirmation& confirmation);

    // Configuration
    void setLogLevel(LogLevel level);
    void setRotateSize(size_t max_size_bytes);
    void enableConsoleOutput(bool enable);

  private:
    std::string log_file_path_;
    std::ofstream log_file_;
    LogLevel current_log_level_;
    size_t max_file_size_;
    bool console_output_enabled_;
    uint64_t next_confirmation_id_;

    void writeLog(LogLevel level, const std::string& message);
    std::string formatLogEntry(LogLevel level, const std::string& message);
    std::string getCurrentTimestamp();
    void rotateLogFile();
    std::string generateConfirmationId();
};

}  // namespace logging
}  // namespace trading