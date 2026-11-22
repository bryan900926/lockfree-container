#include "./memory/StandardAllocator.hpp"
#include "hp/HpManager.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>
#include <type_traits>

#pragma once

namespace yao_lf {

#ifdef YAO_BENCH
struct EliminationStats {
  uint64_t attempts{0};
  uint64_t successes{0};

  ~EliminationStats() {
    if (attempts > 0) {
      double rate = (static_cast<double>(successes) / attempts) * 100.0;
      printf("Thread [%d] Elimination Success Rate: %.2f%% (%lu / %lu)\n",
             std::this_thread::get_id(), rate, successes, attempts);
    }
  }
};

inline EliminationStats &get_stats() {
  thread_local EliminationStats stats;
  return stats;
}
#endif

inline uint32_t fast_rand() {
  // Initialized once per thread
  thread_local uint32_t state =
      reinterpret_cast<uintptr_t>(&state) + // Mix stack address
      std::hash<std::thread::id>{}(std::this_thread::get_id());
  if (state == 0)
    state = 1;

  // Xorshift32 algorithm (extremely fast, no locks)
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}
template <typename T, typename Allocator = yao_mem::StandardAllocator<T>>
class TrieberStack {

  static_assert(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>,
                "T must be copyable!!");

private:
  struct Node {
    T data;
    Node *next{nullptr};
    Node() = default;
    Node(const T &data) : data(data) {}
  };
  using NodeAllocator =
      typename std::allocator_traits<Allocator>::template rebind_alloc<Node>;
  std::atomic<Node *> head{nullptr};

  // Prevent False Sharing by padding the atomic to 64 bytes
  struct alignas(64) PaddedAtomic {
    std::atomic<Node *> slot{nullptr};
  };

  std::vector<PaddedAtomic> eliminated_arr_;
  alignas(64) int len_;

public:
  TrieberStack() {
    unsigned int target_threads =
        std::max(8u, std::thread::hardware_concurrency());
    unsigned int capacity = 1;
    while (capacity < target_threads) {
      capacity <<= 1;
    }
    eliminated_arr_ = std::vector<PaddedAtomic>(capacity);
    len_ = eliminated_arr_.size();
  }

  void push(const T &data) {
    // Correct allocation and construction matching std::allocator_traits
    NodeAllocator alloc;
    Node *new_node = std::allocator_traits<NodeAllocator>::allocate(alloc, 1);
    std::allocator_traits<NodeAllocator>::construct(alloc, new_node, data);

    for (int s{}; 1 << s < len_; ++s) {
      // Try elimination array first during high contention
      int inserted_idx = fast_rand() & ((1 << s) - 1);
      Node *expected = nullptr;

      // Try to place our node in the elimination slot
      if (eliminated_arr_[inserted_idx].slot.compare_exchange_strong(
              expected, new_node, std::memory_order_release,
              std::memory_order_relaxed)) {
#ifdef YAO_BENCH
        get_stats().attempts++;
#endif
        for (int i{}; i < 3; ++i)
          __builtin_ia32_pause();

        Node *expected_remove = new_node;
        if (!eliminated_arr_[inserted_idx].slot.compare_exchange_strong(
                expected_remove, nullptr, std::memory_order_acquire,
                std::memory_order_relaxed)) {

#ifdef YAO_BENCH
          get_stats().successes++;
#endif
          return;
        }
        break;
      }
    }

    // High contention fallback: push to the main Trieber Stack
    while (true) {
      new_node->next = head.load(std::memory_order_relaxed);
      if (head.compare_exchange_weak(new_node->next, new_node,
                                     std::memory_order_release,
                                     std::memory_order_relaxed)) {
        break;
      }
    }
  }

  bool pop(T &data) {
    // Try to find a node in the elimination array first
    for (int s{}; 1 << s < len_; ++s) {
      int taken_idx = fast_rand() & ((1 << s) - 1);
      Node *expected =
          eliminated_arr_[taken_idx].slot.load(std::memory_order_relaxed);

      if (expected != nullptr) {
        // Try to claim the node by swapping it with nullptr
        if (eliminated_arr_[taken_idx].slot.compare_exchange_strong(
                expected, nullptr, std::memory_order_acquire,
                std::memory_order_relaxed)) {

          data = expected->data;

          // Clean up the node safely
          NodeAllocator alloc;
          std::allocator_traits<NodeAllocator>::destroy(alloc, expected);
          std::allocator_traits<NodeAllocator>::deallocate(alloc, expected, 1);
          return true;
        }
      }
    }

    // Fallback: pop from the main Trieber Stack using Hazard Pointers
    hp::ThreadHpOwner<T> &hp_owner =
        hp::ThreadHpOwner<T>::get_thread_hp_owner();
    Node *old_head = head.load(std::memory_order_relaxed);

    while (true) {
      if (old_head == nullptr) {
        return false; // Stack is empty
      }

      hp_owner.protect(old_head);

      // Verify head hasn't changed after protection
      if (head.load(std::memory_order_acquire) != old_head) {
        old_head = head.load(std::memory_order_relaxed);
        continue;
      }

      Node *next_node = old_head->next;

      if (head.compare_exchange_weak(old_head, next_node,
                                     std::memory_order_release,
                                     std::memory_order_relaxed)) {
        break;
      }
    }

    try {
      data = old_head->data;
    } catch (...) {
      hp_owner.clear();
      hp::retire<Node, NodeAllocator>(old_head);
      throw;
    }

    hp_owner.clear();
    hp::retire<Node, NodeAllocator>(old_head);
    return true;
  }

  // delete the yao_mem::Node<T> resource created by push
  ~TrieberStack() {
    Node *current = head.load(std::memory_order_relaxed);
    NodeAllocator alloc;
    while (current != nullptr) {
      Node *next = current->next;
      std::allocator_traits<NodeAllocator>::destroy(alloc, current);
      std::allocator_traits<NodeAllocator>::deallocate(alloc, current, 1);
      current = next;
    }
  }
};
} // namespace yao_lf
