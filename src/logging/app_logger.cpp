#include "trading/logging/app_logger.hpp"
#include "trading/logging/log_level.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace trading {
namespace logging {

AppLogger::AppLogger(const std::string& log_file_path)
    : AsyncLogger(log_file_path),
      current_log_level_(LogLevel::INFO),
      console_output_enabled_(true) {
}

void AppLogger::log(LogLevel level, const std::string& message) {
    if (level < current_log_level_) {
        return;
    }

    std::string formatted_message = formatLogEntry(level, message);

    // Add to async queue for file logging
    addLog(formatted_message);

    // Handle console output immediately (synchronously)
    if (console_output_enabled_) {
        // Use std::cout for INFO/DEBUG, std::cerr for WARNING/ERROR
        if (level >= LogLevel::WARNING) {
            std::cerr << formatted_message << std::endl;
        } else {
            std::cout << formatted_message << std::endl;
        }
    }
}

void AppLogger::setLogLevel(LogLevel level) {
    current_log_level_ = level;
}

void AppLogger::enableConsoleOutput(bool enable) {
    console_output_enabled_ = enable;
}

std::string AppLogger::formatLogEntry(LogLevel level, const std::string& message) {
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

std::string AppLogger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

}  // namespace logging
}  // namespace trading