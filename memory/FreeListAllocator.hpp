#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <utility>

#pragma once

namespace yao_mem {
template <typename T, std::size_t CAP = 1024> class FreeListAllocator {

public:
  using value_type = T;

  template <typename U> struct rebind {
    using other = yao_mem::FreeListAllocator<U, CAP>;
  };

  [[nodiscard]] static T *allocate() {
    return FreeList::get_thread_allocator().allocate();
  }
  static void disallocate(void *ptr) {
    return FreeList::get_thread_allocator().disallocate(ptr);
  }

private:
  class FreeList {
  public:
    union Chunk {
      Chunk *next;
      alignas(T) std::byte data[sizeof(T)];
    };

  private:
    // --- TIER 2: GLOBAL SHARED POOL ---
    // Static members shared across all threads
    struct GlobalPool {
      std::mutex mtx;
      Chunk *head{nullptr};
    };

    static GlobalPool &get_global_pool() {
      static GlobalPool pool;
      return pool;
    }

  public:
    FreeList() = default;

    ~FreeList() {
      // Clean up thread-local nodes
      while (head_) {
        Chunk *next = head_->next;
        delete head_;
        head_ = next;
      }
    }

    static FreeList &get_thread_allocator() {
      thread_local FreeList fl;
      return fl;
    }

    template <typename... Args> [[nodiscard]] T *allocate(Args &&...args) {
      if (!head_) {
        // FAST PATH FAILED: Local list empty.
        // Try to steal a batch from the global pool.
        GlobalPool &global = get_global_pool();
        std::lock_guard<std::mutex> lock(global.mtx);

        // Take up to CAP/2 items from global to refill local
        std::size_t stolen = 0;
        while (global.head && stolen < CAP / 2) {
          Chunk *chunk = global.head;
          global.head = global.head->next;

          chunk->next = head_;
          head_ = chunk;
          stolen++;
          count_++;
        }
      }

      Chunk *chunk;
      if (head_) {
        chunk = head_;
        head_ = head_->next;
        count_--;
      } else {
        // GLOBAL POOL ALSO EMPTY: Resort to OS allocation
        chunk = new Chunk;
      }
      return new (&chunk->data) T(std::forward<Args>(args)...);
    }

    void disallocate(void *ptr) {
      if (!ptr)
        return;

      auto deleted_ptr = static_cast<T *>(ptr);
      deleted_ptr->~T();
      Chunk *chunk = reinterpret_cast<Chunk *>(deleted_ptr);

      if (count_ >= CAP) {
        // FAST PATH FAILED: Local list full.
        // Flush half of our local list to the global pool.
        Chunk *flush_head = head_;
        Chunk *flush_tail = head_;

        // Find the midpoint to break the chain
        for (std::size_t i = 0; i < (CAP / 2) - 1; ++i) {
          flush_tail = flush_tail->next;
        }

        // Disconnect the batch from local list
        head_ = flush_tail->next;
        flush_tail->next = nullptr;
        count_ -= (CAP / 2);

        // Push the entire batch to the global pool in O(1) time
        GlobalPool &global = get_global_pool();
        std::lock_guard<std::mutex> lock(global.mtx);
        flush_tail->next = global.head;
        global.head = flush_head;
      }

      // Add to local list
      chunk->next = head_;
      head_ = chunk;
      count_++;
    }

  private:
    Chunk *head_{nullptr};
    std::size_t count_{0};
  };
};
} // namespace yao_mem
