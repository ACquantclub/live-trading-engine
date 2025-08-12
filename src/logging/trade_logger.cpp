#include "trading/logging/trade_logger.hpp"
#include "trading/logging/log_level.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace trading {
namespace logging {

TradeLogger::TradeLogger(const std::string& log_file_path)
    : AsyncLogger(log_file_path),
      current_log_level_(LogLevel::INFO),
      max_file_size_(100 * 1024 * 1024),
      console_output_enabled_(true),
      next_confirmation_id_(1) {
}

void TradeLogger::logTrade(const core::Trade& trade) {
    std::ostringstream oss;
    oss << "TRADE: " << trade.trade_id << " Symbol: " << trade.symbol
        << " Quantity: " << trade.quantity << " Price: " << trade.price
        << " Buy Order: " << trade.buy_order_id << " Sell Order: " << trade.sell_order_id;

    std::string formatted_message = formatLogEntry(LogLevel::INFO, oss.str());

    // Add to async queue for file logging
    addLog(formatted_message);

    // Handle console output immediately (synchronously)
    if (console_output_enabled_) {
        std::cout << formatted_message << std::endl;
    }
}

void TradeLogger::logExecution(const execution::ExecutionResult& result) {
    std::ostringstream oss;
    oss << "EXECUTION: " << result.execution_id << " Status: " << static_cast<int>(result.status)
        << " Quantity: " << result.executed_quantity << " Price: " << result.executed_price;
    if (!result.error_message.empty()) {
        oss << " Error: " << result.error_message;
    }

    std::string formatted_message = formatLogEntry(LogLevel::INFO, oss.str());

    // Add to async queue for file logging
    addLog(formatted_message);

    // Handle console output immediately (synchronously)
    if (console_output_enabled_) {
        std::cout << formatted_message << std::endl;
    }
}

void TradeLogger::logMessage(LogLevel level, const std::string& message) {
    if (level < current_log_level_) {
        return;
    }

    std::string formatted_message = formatLogEntry(level, message);

    // Add to async queue for file logging
    addLog(formatted_message);

    // Handle console output immediately (synchronously)
    if (console_output_enabled_) {
        std::cout << formatted_message << std::endl;
    }
}

TradeConfirmation TradeLogger::createConfirmation(const core::Trade& trade) {
    TradeConfirmation confirmation;
    confirmation.confirmation_id = generateConfirmationId();
    confirmation.trade_id = trade.trade_id;
    confirmation.symbol = trade.symbol;
    confirmation.quantity = trade.quantity;
    confirmation.price = trade.price;
    confirmation.timestamp = trade.timestamp;
    confirmation.status = "CONFIRMED";
    return confirmation;
}

bool TradeLogger::sendConfirmation(const TradeConfirmation& confirmation) {
    // TODO: Implement confirmation sending (could be email, message queue, etc.)
    std::ostringstream oss;
    oss << "CONFIRMATION: " << confirmation.confirmation_id << " Trade: " << confirmation.trade_id
        << " Status: " << confirmation.status;

    std::string formatted_message = formatLogEntry(LogLevel::INFO, oss.str());

    // Add to async queue for file logging
    addLog(formatted_message);

    // Handle console output immediately (synchronously)
    if (console_output_enabled_) {
        std::cout << formatted_message << std::endl;
    }

    return true;
}

void TradeLogger::setLogLevel(LogLevel level) {
    current_log_level_ = level;
}

void TradeLogger::setRotateSize(size_t max_size_bytes) {
    max_file_size_ = max_size_bytes;
}

void TradeLogger::enableConsoleOutput(bool enable) {
    console_output_enabled_ = enable;
}

std::string TradeLogger::formatLogEntry(LogLevel level, const std::string& message) {
    std::ostringstream oss;
    oss << "[" << getCurrentTimestamp() << "] ";

    switch (level) {
        case LogLevel::DEBUG:
            oss << "[DEBUG] ";
            break;
        case LogLevel::INFO:
            oss << "[INFO]  ";
            break;
        case LogLevel::WARNING:
            oss << "[WARN]  ";
            break;
        case LogLevel::ERROR:
            oss << "[ERROR] ";
            break;
    }

    oss << message;
    return oss.str();
}

std::string TradeLogger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

void TradeLogger::rotateLogFile() {
    // TODO: Implement log file rotation
}

std::string TradeLogger::generateConfirmationId() {
    return "CONF_" + std::to_string(next_confirmation_id_++);
}

}  // namespace logging
}  // namespace trading