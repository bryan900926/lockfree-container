#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <mutex>

#pragma once

namespace yao_mem {
template <typename T, std::size_t CAP = 1024> class FreeListAllocator {
public:
  using value_type = T;

  template <typename U> struct rebind {
    using other = yao_mem::FreeListAllocator<U, CAP>;
  };

  [[nodiscard]] T *allocate(std::size_t n = 1) {
    if (n != 1) {
      // FreeList only handles single objects. Fallback to OS for arrays.
      return static_cast<T *>(::operator new(n * sizeof(T)));
    }
    return FreeList::get_thread_allocator().allocate();
  }

  void deallocate(T *ptr, std::size_t n = 1) {
    if (!ptr)
      return;
    if (n != 1) {
      ::operator delete(ptr);
      return;
    }
    FreeList::get_thread_allocator().deallocate(ptr);
  }

  template <class U, class... Args> void construct(U *p, Args &&...args) {
    ::new (static_cast<void *>(p)) U(std::forward<Args>(args)...);
  }

  template <class U> void destroy(U *p) { p->~U(); }

  // deleter used when thread local freelist destroy first
  void static fall_back_deleter(T *p) {
    p->~T();
    // Chunk has the same memory layout, so we can cast and raw delete
    delete reinterpret_cast<typename FreeList::Chunk *>(p);
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

      ~GlobalPool() {
        std::lock_guard<std::mutex> lg(mtx);
        while (head) {
          Chunk *next = head->next;
          delete head;
          head = next;
        }
      }
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
        count_--;
        head_ = next;
      }
    }

    static FreeList &get_thread_allocator() {
      thread_local FreeList fl;
      return fl;
    }

    [[nodiscard]] T *allocate() {
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
      return reinterpret_cast<T *>(chunk);
    }

    void deallocate(void *ptr) {
      if (!ptr)
        return;

      Chunk *chunk = reinterpret_cast<Chunk *>(ptr);

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
