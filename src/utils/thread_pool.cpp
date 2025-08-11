#include "trading/utils/thread_pool.hpp"

namespace trading::utils {

ThreadPool::ThreadPool(size_t threads) : stop(false) {
    if (threads == 0) {
        threads = std::thread::hardware_concurrency();
    }

    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;

                // Lock the mutex to protect the shared queue
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);

                    // Wait for a task to be available or for the pool to stop
                    this->condition.wait(lock,
                                         [this] { return this->stop || !this->tasks.empty(); });

                    // If the pool is stopping and there are no more tasks, exit
                    if (this->stop && this->tasks.empty()) {
                        return;
                    }

                    // Get the task from the queue
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }

                // Execute the task outside the lock
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    // Lock the queue to set the stop flag
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }

    // Notify all worker threads to wake u
    condition.notify_all();

    // Join all threads to ensure they complete their tasks and exit
    for (std::thread& worker : workers) {
        worker.join();
    }
}

}  // namespace trading::utils