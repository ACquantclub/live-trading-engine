#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>
#include <vector>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trading/utils/thread_safe_queue.hpp"

using namespace trading::utils;

// Test fixture for the ThreadSafeQueue tests
class ThreadSafeQueueTest : public ::testing::Test {
  protected:
    ThreadSafeQueue<int> queue_;
};

// Test case for basic push and pop operations in a single thread
TEST_F(ThreadSafeQueueTest, SingleThreadedPushAndPop) {
    ASSERT_TRUE(queue_.empty());
    ASSERT_EQ(queue_.size(), 0);

    // Push an item and check the state
    queue_.push(42);
    ASSERT_FALSE(queue_.empty());
    ASSERT_EQ(queue_.size(), 1);

    // Pop the item and verify its value
    int value = queue_.wait_and_pop();
    EXPECT_EQ(value, 42);
    ASSERT_TRUE(queue_.empty());
    ASSERT_EQ(queue_.size(), 0);
}

// Test case for the non-blocking try_pop method
TEST_F(ThreadSafeQueueTest, TryPopBehavior) {
    // try_pop on an empty queue should return nullopt
    auto result = queue_.try_pop();
    ASSERT_FALSE(result.has_value());

    // Push an item and try_pop it
    queue_.push(100);
    result = queue_.try_pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 100);

    // The queue should be empty again
    ASSERT_TRUE(queue_.empty());
}

// Test case for a single producer and a single consumer thread
TEST_F(ThreadSafeQueueTest, SingleProducerSingleConsumer) {
    std::vector<int> produced_items;
    std::vector<int> consumed_items;
    const int num_items = 100;

    // Producer thread
    std::thread producer([this, &produced_items, num_items]() {
        for (int i = 0; i < num_items; ++i) {
            queue_.push(i);
            produced_items.push_back(i);
        }
    });

    // Consumer thread
    std::thread consumer([this, &consumed_items, num_items]() {
        for (int i = 0; i < num_items; ++i) {
            consumed_items.push_back(queue_.wait_and_pop());
        }
    });

    producer.join();
    consumer.join();

    // Verify that all produced items were consumed in the correct order
    ASSERT_EQ(produced_items.size(), num_items);
    ASSERT_EQ(consumed_items.size(), num_items);
    EXPECT_EQ(produced_items, consumed_items);
    ASSERT_TRUE(queue_.empty());
}

// Test case for multiple producers and a single consumer
TEST_F(ThreadSafeQueueTest, MultiProducerSingleConsumer) {
    const int num_producers = 4;
    const int items_per_producer = 50;
    const int total_items = num_producers * items_per_producer;
    std::atomic<int> produced_sum{0};
    std::atomic<int> consumed_sum{0};

    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([this, &produced_sum, i, items_per_producer]() {
            for (int j = 0; j < items_per_producer; ++j) {
                int value = i * items_per_producer + j;
                queue_.push(value);
                produced_sum += value;
            }
        });
    }

    // Consumer thread
    std::thread consumer([this, &consumed_sum, total_items]() {
        for (int i = 0; i < total_items; ++i) {
            consumed_sum += queue_.wait_and_pop();
        }
    });

    for (auto& p : producers) {
        p.join();
    }
    consumer.join();

    // The sum of all consumed items should equal the sum of all produced items
    ASSERT_EQ(produced_sum.load(), consumed_sum.load());
    ASSERT_TRUE(queue_.empty());
}

// Test case for multiple producers and multiple consumers
TEST_F(ThreadSafeQueueTest, MultiProducerMultiConsumer) {
    const int num_producers = 5;
    const int num_consumers = 5;
    const int items_per_producer = 100;
    const int total_items = num_producers * items_per_producer;
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};

    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([this, &produced_count, items_per_producer]() {
            for (int j = 0; j < items_per_producer; ++j) {
                queue_.push(j);
                produced_count++;
            }
        });
    }

    std::vector<std::thread> consumers;
    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([this, &consumed_count, total_items, num_consumers]() {
            // Each consumer will try to consume a fraction of the total items
            for (int j = 0; j < total_items / num_consumers; ++j) {
                queue_.wait_and_pop();
                consumed_count++;
            }
        });
    }

    for (auto& p : producers) {
        p.join();
    }
    for (auto& c : consumers) {
        c.join();
    }

    // All produced items should have been consumed
    ASSERT_EQ(produced_count.load(), total_items);
    ASSERT_EQ(consumed_count.load(), total_items);
    ASSERT_TRUE(queue_.empty());
}
