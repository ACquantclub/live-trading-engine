#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace trading::utils {
class ThreadPool {
  public:
    // Constructor initializes the thread pool with a given number of threads
    explicit ThreadPool(size_t threads);

    // Destructor stops all threads and joins them
    ~ThreadPool();

    // Enqueue a task to be executed by a worker thread
    template <class F>
    void enqueue(F&& f) {
        // Wrap the task in a std::function to store it in the queue.
        auto task_ptr = std::make_shared<std::packaged_task<void()>>(std::forward<F>(f));

        // Lock the mutex to protect the shared queue.
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // Don't allow enqueuing after the pool has been stopped.
            if (stop) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            // Add the task to the queue.
            tasks.emplace([task_ptr]() { (*task_ptr)(); });
        }

        // Notify one worker thread that a new task is available.
        condition.notify_one();
    }

  private:
    // Vector of worker threads
    std::vector<std::thread> workers;

    // Task queue
    std::queue<std::function<void()>> tasks;

    // Mutex for synchronization
    std::mutex queue_mutex;

    // Condition variable for task synchronization
    std::condition_variable condition;

    // Flag to indicate if the thread pool is stopping
    bool stop = false;
};
}  // namespace trading::utils