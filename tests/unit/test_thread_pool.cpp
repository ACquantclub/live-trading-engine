#include <chrono>
#include <thread>
#include <vector>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trading/utils/thread_pool.hpp"

using namespace trading::utils;

class ThreadPoolTest : public ::testing::Test {
  protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

// Test case for basic task execution
TEST_F(ThreadPoolTest, EnqueuesAndExecutesTask) {
    bool task_executed = false;
    {
        // Create a thread pool with a single worker threa.
        ThreadPool pool(1);

        // Enqueue a lambda task that sets the boolean flag
        pool.enqueue([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            task_executed = true;
        });
    }  // The thread pool destructor is called here, which waits for the task to finish

    // Assert that the task was executed
    ASSERT_TRUE(task_executed);
}

// Test case to verify that multiple tasks are executed concurrently
TEST_F(ThreadPoolTest, HandlesMultipleTasks) {
    std::atomic<int> counter{0};
    const int num_tasks = 100;
    const int num_threads = 4;

    {
        ThreadPool pool(num_threads);

        // Enqueue a large number of tasks
        for (int i = 0; i < num_tasks; ++i) {
            pool.enqueue([&]() { counter++; });
        }
    }  // Pool destructor waits for all tasks to complete

    // All tasks should have been executed
    ASSERT_EQ(counter.load(), num_tasks);
}

// Test case for the graceful shutdown behavior
TEST_F(ThreadPoolTest, ShutsDownGracefully) {
    bool task_started = false;
    bool task_finished = false;
    {
        ThreadPool pool(1);

        // Enqueue a task that simulates a long operation
        pool.enqueue([&]() {
            task_started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            task_finished = true;
        });

        // Main thread continues and should not block here
        ASSERT_FALSE(task_finished);
    }

    // After the pool is destroyed, the task must have completed
    ASSERT_TRUE(task_started);
    ASSERT_TRUE(task_finished);
}

// Test case for handling a large number of tasks to check for queueing
TEST_F(ThreadPoolTest, QueuesTasksWhenThreadsAreBusy) {
    std::atomic<int> active_workers{0};
    std::mutex mtx;
    std::condition_variable cv;

    // Use a pool with a limited number of threads to force tasks into the queue
    ThreadPool pool(2);

    // Enqueue 4 tasks that will block for a short time
    for (int i = 0; i < 4; ++i) {
        pool.enqueue([&]() {
            active_workers++;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            active_workers--;
        });
    }

    // Give some time for the first threads to start.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // At this point, we should see up to 2 active workers, not more
    // The other tasks should be waiting in the queue
    ASSERT_LE(active_workers.load(), 2);
}