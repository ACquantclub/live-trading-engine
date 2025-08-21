#include <atomic>
#include <chrono>
#include <numeric>
#include <thread>
#include <vector>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "trading/utils/concurrent_queue.hpp"

using namespace trading::utils;

// Test fixture for the ConcurrentQueue tests
class ConcurrentQueueTest : public ::testing::Test {
  protected:
    static constexpr size_t kDefaultCapacity = 16;
    ConcurrentQueue<int> queue_{kDefaultCapacity};
};

// Test case for basic construction and capacity
TEST_F(ConcurrentQueueTest, ConstructionAndCapacity) {
    // Test that capacity is rounded up to next power of 2
    ConcurrentQueue<int> small_queue(3);
    EXPECT_EQ(small_queue.capacity(), 4);  // 3 rounded up to 4

    ConcurrentQueue<int> medium_queue(10);
    EXPECT_EQ(medium_queue.capacity(), 16);  // 10 rounded up to 16

    ConcurrentQueue<int> large_queue(100);
    EXPECT_EQ(large_queue.capacity(), 128);  // 100 rounded up to 128
}

// Test case for invalid construction
TEST_F(ConcurrentQueueTest, InvalidConstruction) {
    EXPECT_THROW(ConcurrentQueue<int>(0), std::invalid_argument);
}

// Test case for basic enqueue and dequeue operations in a single thread
TEST_F(ConcurrentQueueTest, SingleThreadedEnqueueAndDequeue) {
    EXPECT_EQ(queue_.size(), 0);

    // Enqueue an item and check the state
    int item = 42;
    queue_.enqueue(std::move(item));
    EXPECT_EQ(queue_.size(), 1);

    // Dequeue the item and verify its value
    int value;
    ASSERT_TRUE(queue_.try_dequeue(value));
    EXPECT_EQ(value, 42);
    EXPECT_EQ(queue_.size(), 0);
}

// Test case for the try_dequeue method on empty queue
TEST_F(ConcurrentQueueTest, TryDequeueOnEmptyQueue) {
    int value;
    ASSERT_FALSE(queue_.try_dequeue(value));
    EXPECT_EQ(queue_.size(), 0);
}

// Test case for FIFO ordering
TEST_F(ConcurrentQueueTest, FIFOOrdering) {
    const int num_items = 10;

    // Enqueue items in order
    for (int i = 0; i < num_items; ++i) {
        queue_.enqueue(std::move(i));
    }

    EXPECT_EQ(queue_.size(), num_items);

    // Dequeue items and verify order
    for (int i = 0; i < num_items; ++i) {
        int value;
        ASSERT_TRUE(queue_.try_dequeue(value));
        EXPECT_EQ(value, i);
    }

    EXPECT_EQ(queue_.size(), 0);
}

// Test case for filling the queue to capacity
TEST_F(ConcurrentQueueTest, FillToCapacity) {
    const size_t capacity = queue_.capacity();

    // Fill the queue to capacity
    for (size_t i = 0; i < capacity; ++i) {
        int value = static_cast<int>(i);
        queue_.enqueue(std::move(value));
    }

    EXPECT_EQ(queue_.size(), capacity);

    // Dequeue all items
    for (size_t i = 0; i < capacity; ++i) {
        int value;
        ASSERT_TRUE(queue_.try_dequeue(value));
        EXPECT_EQ(value, static_cast<int>(i));
    }

    EXPECT_EQ(queue_.size(), 0);
}

// Test case for a single producer and a single consumer thread
TEST_F(ConcurrentQueueTest, SingleProducerSingleConsumer) {
    std::vector<int> produced_items;
    std::vector<int> consumed_items;
    const int num_items = 1000;

    // Producer thread
    std::thread producer([this, &produced_items, num_items]() {
        for (int i = 0; i < num_items; ++i) {
            queue_.enqueue(std::move(i));
            produced_items.push_back(i);
        }
    });

    // Consumer thread
    std::thread consumer([this, &consumed_items, num_items]() {
        int consumed = 0;
        while (consumed < num_items) {
            int value;
            if (queue_.try_dequeue(value)) {
                consumed_items.push_back(value);
                consumed++;
            } else {
                // Brief yield to avoid busy spinning
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // Verify that all produced items were consumed in the correct order
    ASSERT_EQ(produced_items.size(), num_items);
    ASSERT_EQ(consumed_items.size(), num_items);
    EXPECT_EQ(produced_items, consumed_items);
    EXPECT_EQ(queue_.size(), 0);
}

// Test case for multiple producers and a single consumer
TEST_F(ConcurrentQueueTest, MultiProducerSingleConsumer) {
    const int num_producers = 4;
    const int items_per_producer = 250;
    const int total_items = num_producers * items_per_producer;
    std::atomic<long long> produced_sum{0};
    std::atomic<long long> consumed_sum{0};

    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([this, &produced_sum, i, items_per_producer]() {
            for (int j = 0; j < items_per_producer; ++j) {
                int value = i * items_per_producer + j;
                queue_.enqueue(std::move(value));
                produced_sum += value;
            }
        });
    }

    // Consumer thread
    std::thread consumer([this, &consumed_sum, total_items]() {
        int consumed = 0;
        while (consumed < total_items) {
            int value;
            if (queue_.try_dequeue(value)) {
                consumed_sum += value;
                consumed++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (auto& p : producers) {
        p.join();
    }
    consumer.join();

    // The sum of all consumed items should equal the sum of all produced items
    EXPECT_EQ(produced_sum.load(), consumed_sum.load());
    EXPECT_EQ(queue_.size(), 0);
}

// Test case for stress testing with high contention
TEST_F(ConcurrentQueueTest, HighContentionStressTest) {
    const int num_producers = 8;
    const int items_per_producer = 500;
    const int total_items = num_producers * items_per_producer;
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};

    // Use a larger queue for this test
    ConcurrentQueue<int> large_queue(1024);

    std::vector<std::thread> producers;
    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&large_queue, &produced_count, items_per_producer]() {
            for (int j = 0; j < items_per_producer; ++j) {
                large_queue.enqueue(std::move(j));
                produced_count++;
            }
        });
    }

    // Single consumer thread
    std::thread consumer([&large_queue, &consumed_count, total_items]() {
        int consumed = 0;
        while (consumed < total_items) {
            int value;
            if (large_queue.try_dequeue(value)) {
                consumed_count++;
                consumed++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (auto& p : producers) {
        p.join();
    }
    consumer.join();

    // All produced items should have been consumed
    EXPECT_EQ(produced_count.load(), total_items);
    EXPECT_EQ(consumed_count.load(), total_items);
    EXPECT_EQ(large_queue.size(), 0);
}

// Test case for performance benchmark
TEST_F(ConcurrentQueueTest, PerformanceBenchmark) {
    const int num_operations = 100000;
    ConcurrentQueue<int> perf_queue(8192);

    auto start = std::chrono::high_resolution_clock::now();

    // Producer thread
    std::thread producer([&perf_queue, num_operations]() {
        for (int i = 0; i < num_operations; ++i) {
            perf_queue.enqueue(std::move(i));
        }
    });

    // Consumer thread
    std::thread consumer([&perf_queue, num_operations]() {
        int consumed = 0;
        while (consumed < num_operations) {
            int value;
            if (perf_queue.try_dequeue(value)) {
                consumed++;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // This is just a performance indicator, not a hard requirement
    std::cout << "Processed " << num_operations << " items in " << duration.count()
              << " microseconds (" << (num_operations * 1000000.0 / duration.count()) << " ops/sec)"
              << std::endl;

    EXPECT_EQ(perf_queue.size(), 0);
}

// Test case for move semantics
TEST_F(ConcurrentQueueTest, MoveSemantics) {
    struct MovableType {
        int value;
        bool moved_from = false;

        MovableType(int v) : value(v) {
        }
        MovableType(MovableType&& other) : value(other.value) {
            other.moved_from = true;
        }
        MovableType& operator=(MovableType&& other) {
            value = other.value;
            other.moved_from = true;
            return *this;
        }

        // Disable copy to ensure move semantics are used
        MovableType(const MovableType&) = delete;
        MovableType& operator=(const MovableType&) = delete;
    };

    ConcurrentQueue<MovableType> move_queue(16);

    MovableType item(42);
    move_queue.enqueue(std::move(item));
    EXPECT_TRUE(item.moved_from);

    MovableType result(0);
    ASSERT_TRUE(move_queue.try_dequeue(result));
    EXPECT_EQ(result.value, 42);
}

// Test case for exception safety
TEST_F(ConcurrentQueueTest, ExceptionSafety) {
    // Move the static member outside of the local class
    static bool should_throw = false;

    struct ThrowingType {
        int value;

        ThrowingType(int v) : value(v) {
            if (should_throw) {
                throw std::runtime_error("Test exception");
            }
        }

        ThrowingType(ThrowingType&& other) : value(other.value) {
        }
        ThrowingType& operator=(ThrowingType&& other) {
            value = other.value;
            return *this;
        }
    };

    ConcurrentQueue<ThrowingType> throw_queue(16);

    // Normal operation should work
    should_throw = false;
    ThrowingType item(10);
    throw_queue.enqueue(std::move(item));

    ThrowingType result(0);
    ASSERT_TRUE(throw_queue.try_dequeue(result));
    EXPECT_EQ(result.value, 10);
}