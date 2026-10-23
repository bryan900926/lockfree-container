#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

#include "TreiberStack.hpp"

namespace yao_lf {

template <typename T, size_t BLOCK_SIZE = 512> class SPSCQueue {
  static_assert(std::atomic<size_t>::is_always_lock_free,
                "size_t is not always lock free :(");
  static_assert(BLOCK_SIZE >= 2 && (BLOCK_SIZE & (BLOCK_SIZE - 1)) == 0,
                "BLOCK_SIZE must be 2^n and n >= 1 !!");
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable");

private:
  struct Block {
    std::array<T, BLOCK_SIZE> buffer;
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};
    std::atomic<Block *> next{nullptr}; // Link FORWARD
  };

  TrieberStack<Block *> free_blocks_;

  // Separate the pointers to avoid false sharing and data races
  alignas(64) Block *head_block_; // Consumer ownership
  alignas(64) Block *tail_block_; // Producer ownership

public:
  SPSCQueue() {
    Block *initial_block = new Block();
    head_block_ = initial_block;
    tail_block_ = initial_block;
  }
  ~SPSCQueue() {
    // 1. Delete active blocks in the queue
    for (Block *cur = head_block_; cur;) {
      auto next = cur->next.load(std::memory_order_relaxed);
      delete cur;
      cur = next;
    }

    // 2. Delete the actual Block objects stored inside the stack
    Block *cached_block = nullptr;
    while (free_blocks_.pop(cached_block)) {
      delete cached_block; // <--- This reclaims the type T (the Block itself)
    }
  }
  // only one writer thread would use this function
  void push(const T &data) {
    size_t cur_tail = tail_block_->tail.load(std::memory_order_relaxed);

    // If current block is completely full (linear, no wrap-around)
    if (cur_tail == BLOCK_SIZE) {
      Block *next_block{nullptr};
      if (!free_blocks_.pop(next_block)) {
        next_block = new Block();
      } else {
        // Reset recycled block state
        next_block->head.store(0, std::memory_order_relaxed);
        next_block->tail.store(0, std::memory_order_relaxed);
        next_block->next.store(nullptr, std::memory_order_relaxed);
      }

      // Link forward and publish to consumer
      tail_block_->next.store(next_block, std::memory_order_release);
      tail_block_ = next_block; // Producer moves to new block
      cur_tail = 0;
    }
    tail_block_->buffer[cur_tail] = data;
    tail_block_->tail.store(cur_tail + 1, std::memory_order_release);
  }

  bool pop(T &out_data) {
    size_t cur_head = head_block_->head.load(std::memory_order_relaxed);

    // If consumer has exhausted this block
    if (cur_head == BLOCK_SIZE) {
      Block *next_block = head_block_->next.load(std::memory_order_acquire);

      if (!next_block) {
        return false; // Producer hasn't linked the new block yet
      }

      // Move to next block and recycle the old one
      Block *old_block = head_block_;
      head_block_ = next_block;
      free_blocks_.push(old_block);

      cur_head = head_block_->head.load(std::memory_order_relaxed);
    }

    // Check if there is actually data to read in the current block
    if (cur_head == head_block_->tail.load(std::memory_order_acquire)) {
      return false; // Queue is empty
    }

    out_data = head_block_->buffer[cur_head];
    head_block_->head.store(cur_head + 1, std::memory_order_release);
    return true;
  }
};
} // namespace yao_lf
