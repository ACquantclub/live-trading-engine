#pragma once

#include "trading/utils/thread_safe_queue.hpp"
#include <atomic>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace trading::logging {

class AsyncLogger {
  public:
    explicit AsyncLogger(const std::string& log_file_path)
        : log_file_path_(log_file_path), done_(false) {
    }

    virtual ~AsyncLogger() {
        stop();
    }

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;
    AsyncLogger(AsyncLogger&&) = delete;
    AsyncLogger& operator=(AsyncLogger&&) = delete;

    // Start background logging thread
    void start() {
        log_file_.open(log_file_path_, std::ios::app);
        if (!log_file_.is_open()) {
            throw std::runtime_error("Failed to open log file: " + log_file_path_);
        }
        worker_thread_ = std::thread(&AsyncLogger::run, this);
    }

    // Stop background logging thread
    void stop() {
        if (done_.exchange(true)) {
            return;  // Already stopped or stopping
        }

        // Push sentinel to wake up and stop the thread
        log_queue_.push(SENTINEL);
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

        if (log_file_.is_open()) {
            log_file_.close();
        }
    }

  protected:
    // Add a log message to the queue
    void addLog(std::string message) {
        if (!done_) {
            log_queue_.push(std::move(message));
        }
    }

  private:
    // The main function for the worker thread
    void run() {
        while (true) {
            std::string message = log_queue_.wait_and_pop();
            if (message == SENTINEL) {
                // Drain any remaining messages that were queued before stop() was called
                while (auto remaining_msg = log_queue_.try_pop()) {
                    if (*remaining_msg != SENTINEL) {
                        writeLog(*remaining_msg);
                    }
                }
                break;
            }
            writeLog(message);
        }
    }

    // Write a log message to the log file
    void writeLog(const std::string& message) {
        if (log_file_.is_open()) {
            log_file_ << message << std::endl;
            log_file_.flush();
        }
    }

    // A unique string to signal the logging thread to terminate.
    const std::string SENTINEL = "e0a6f5a9-4c4c-4d9b-8e7a-3f1b7f2b1a0e-STOP";

    std::string log_file_path_;
    std::ofstream log_file_;
    utils::ThreadSafeQueue<std::string> log_queue_;
    std::thread worker_thread_;
    std::atomic<bool> done_;
};

}  // namespace trading::logging
