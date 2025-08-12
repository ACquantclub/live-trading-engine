#pragma once

#include <memory>
#include <string>
#include "async_logger.hpp"
#include "log_level.hpp"

namespace trading {
namespace logging {

class AppLogger : public AsyncLogger {
  public:
    explicit AppLogger(const std::string& log_file_path);
    virtual ~AppLogger() = default;

    virtual void log(LogLevel level, const std::string& message);

    void setLogLevel(LogLevel level);
    void enableConsoleOutput(bool enable);

  private:
    LogLevel current_log_level_;
    bool console_output_enabled_;

    std::string formatLogEntry(LogLevel level, const std::string& message);
    std::string getCurrentTimestamp();
};

}  // namespace logging
}  // namespace trading