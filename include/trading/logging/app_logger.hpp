#pragma once

#include <fstream>
#include <memory>
#include <string>
#include "log_level.hpp"

namespace trading {
namespace logging {

class AppLogger {
  public:
    explicit AppLogger(const std::string& log_file_path);
    virtual ~AppLogger();

    AppLogger(const AppLogger&) = delete;
    AppLogger& operator=(const AppLogger&) = delete;
    AppLogger(AppLogger&&) = delete;
    AppLogger& operator=(AppLogger&&) = delete;

    virtual void log(LogLevel level, const std::string& message);

    void setLogLevel(LogLevel level);
    void enableConsoleOutput(bool enable);

  private:
    std::string log_file_path_;
    std::ofstream log_file_;
    LogLevel current_log_level_;
    bool console_output_enabled_;

    void writeLog(LogLevel level, const std::string& message);
    std::string formatLogEntry(LogLevel level, const std::string& message);
    std::string getCurrentTimestamp();
};

}  // namespace logging
}  // namespace trading