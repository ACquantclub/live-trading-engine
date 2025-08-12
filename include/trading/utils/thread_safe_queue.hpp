#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace trading::utils {

template <typename T>
class ThreadSafeQueue {
  public:
    // Push a value onto the queue
    void push(T value) {
        // Lock mutex
        std::lock_guard<std::mutex> lock(mtx_);

        // Move value into queue
        queue_.push(std::move(value));

        // Notify one waiting thread
        cv_.notify_one();
    }

    // Wait and pop a value onto the queue
    T wait_and_pop() {
        std::unique_lock<std::mutex> lock(mtx_);

        // Wait until queue is not empty
        cv_.wait(lock, [this] { return !queue_.empty(); });

        // Retrieve value by moving it
        T value = std::move(queue_.front());
        queue_.pop();

        return value;
    }

    // Try to pop an item from the queue without blocking
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mtx_);

        // If queue is empty, return std::nullopt
        if (queue_.empty()) {
            return std::nullopt;
        }

        // Move value from front of queue
        T value = std::move(queue_.front());
        queue_.pop();

        return value;
    }

    // Return true if queue is empty
    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }

    // Return queue size
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

  private:
    // Mutex to protect access to the queue
    mutable std::mutex mtx_;

    // Underlying queue
    std::queue<T> queue_;

    // Condition variable to signal waiting threads
    std::condition_variable cv_;
};
};  // namespace trading::utils