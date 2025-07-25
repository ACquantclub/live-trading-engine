#pragma once

#include <functional>
#include <memory>
#include "../core/matching_engine.hpp"
#include "../core/order.hpp"

namespace trading {
namespace execution {

enum class ExecutionStatus { SUCCESS, FAILED, PARTIAL, PENDING };

struct ExecutionResult {
    ExecutionStatus status;
    std::string execution_id;
    double executed_quantity;
    double executed_price;
    std::string error_message;
};

class Executor {
  public:
    using ExecutionCallback = std::function<void(const ExecutionResult&)>;

    Executor();
    ~Executor() = default;

    // Execution methods
    ExecutionResult execute(const core::Trade& trade);
    ExecutionResult executeTrade(const std::string& symbol, double quantity, double price);

    // Event handling
    void setExecutionCallback(ExecutionCallback callback);

    // Statistics
    uint64_t getTotalExecutions() const;
    double getTotalExecutedVolume() const;

    // Configuration
    void setTimeout(int milliseconds);
    void setMaxRetries(int max_retries);

  private:
    ExecutionCallback execution_callback_;
    uint64_t total_executions_;
    double total_executed_volume_;
    uint64_t next_execution_id_;
    int timeout_ms_;
    int max_retries_;

    std::string generateExecutionId();
    bool validateExecution(const core::Trade& trade);
    ExecutionResult performExecution(const core::Trade& trade);
};

}  // namespace execution
}  // namespace trading