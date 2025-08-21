#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace trading::utils {

template <typename T>
class ConcurrentQueue {
  public:
    // Constructs the queue with a given capacity
    explicit ConcurrentQueue(size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("Capacity must be non-zero.");
        }

        // Round up to the next power of two
        capacity_ = roundUpToPowerOfTwo(capacity);
        capacity_mask_ = capacity_ - 1;
        buffer_ = std::make_unique<Slot[]>(capacity_);

        // Initialize sequence numbers for each slot
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~ConcurrentQueue() {
        // Manually call the destructor for any non-trivial types left in the queue
        if (!std::is_trivially_destructible_v<T>) {
            size_t current_pos = dequeue_pos_.load();
            while (current_pos != enqueue_pos_.load()) {
                reinterpret_cast<T*>(&buffer_[current_pos & capacity_mask_].storage)->~T();
                current_pos++;
            }
        }
    }

    // Disable copy and move semantics for simplicity and safety
    ConcurrentQueue(const ConcurrentQueue&) = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;
    ConcurrentQueue(ConcurrentQueue&&) = delete;
    ConcurrentQueue& operator=(ConcurrentQueue&&) = delete;

    // Enqueues an item into the queue
    void enqueue(T&& value) {
        // Atomically claim the next slot index
        const size_t pos = enqueue_pos_.fetch_add(1, std::memory_order_relaxed);

        // Find the slot in the ring buffer
        Slot& slot = buffer_[pos & capacity_mask_];

        // Wait efficiently until the slot is available
        size_t current_seq;
        while ((current_seq = slot.sequence.load(std::memory_order_acquire)) != pos) {
            // Atomically check if the sequence is still `current_seq` and, if so, sleep
            slot.sequence.wait(current_seq, std::memory_order_relaxed);
        }

        // Construct the object in place using placement new.
        new (&slot.storage) T(std::move(value));

        // Publish the write by updating the sequence number
        slot.sequence.store(pos + 1, std::memory_order_release);

        // Notify the single consumer thread that a new item might be available
        slot.sequence.notify_one();
    }

    // Dequeues an item from the queue
    bool try_dequeue(T& value) {
        const size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

        Slot& slot = buffer_[pos & capacity_mask_];

        // Check if a producer has finished writing to this slot
        if (slot.sequence.load(std::memory_order_acquire) != pos + 1) {
            return false;
        }

        T* item = reinterpret_cast<T*>(&slot.storage);
        value = std::move(*item);

        // Manually call the destructor since we used placement new
        if (!std::is_trivially_destructible_v<T>) {
            item->~T();
        }

        // Signal to producers that the slot is now free for the next cycle
        slot.sequence.store(pos + capacity_, std::memory_order_release);

        // Notify one potentially waiting producer that this slot is now free
        slot.sequence.notify_one();

        // Move the dequeue position forward
        dequeue_pos_.store(pos + 1, std::memory_order_relaxed);
        return true;
    }

    // Returns the approximate number of items in the queue
    size_t size() const {
        const auto enqueue_pos = enqueue_pos_.load(std::memory_order_acquire);
        const auto dequeue_pos = dequeue_pos_.load(std::memory_order_acquire);
        return (enqueue_pos >= dequeue_pos) ? (enqueue_pos - dequeue_pos) : 0;
    }

    // Returns the total capacity of the queue
    size_t capacity() const {
        return capacity_;
    }

  private:
    // Use a fixed cache line size to avoid ABI compatibility issues
    static constexpr size_t kCacheLineSize = 64;

    // The Slot holds the item and a sequence number for synchronization
    struct Slot {
        std::atomic<size_t> sequence;
        alignas(T) std::byte storage[sizeof(T)];
    };

    // Utility function to round an integer up to the next power of two.
    static size_t roundUpToPowerOfTwo(size_t v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        v++;
        return v;
    }

    // Producer and consumer indices are padded to separate cache lines
    alignas(kCacheLineSize) std::atomic<size_t> enqueue_pos_{0};
    alignas(kCacheLineSize) std::atomic<size_t> dequeue_pos_{0};

    size_t capacity_;
    size_t capacity_mask_;
    std::unique_ptr<Slot[]> buffer_;
};

}  // namespace trading::utils