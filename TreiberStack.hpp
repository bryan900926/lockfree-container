#include "./memory/StandardAllocator.hpp"
#include "hp/HpManager.hpp"

#include <atomic>
#include <memory>
#include <type_traits>

#pragma once

namespace yao_lf {
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

public:
  void push(const T &data) {
    Node *new_node = NodeAllocator::allocate();
    new_node->data = data;
    new_node->next = head.load(std::memory_order_relaxed);
    while (!head.compare_exchange_weak(new_node->next, new_node,
                                       std::memory_order_release,
                                       std::memory_order_relaxed))
      ;
  }

  bool pop(T &data) {
    hp::ThreadHpOwner<T> &hp_owner =
        hp::ThreadHpOwner<T>::get_thread_hp_owner();

    Node *old_head = head.load(std::memory_order_relaxed);

    while (true) {
      // stack is empty
      if (old_head == nullptr) {
        return false;
      }

      hp_owner.protect(old_head);

      // if this condition triggered, other thread might delete this node
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

    // memory resource reclaimed
    try {
      // copy might throw ex
      data = old_head->data;
    } catch (...) {
      hp_owner.clear();
      // let hazard pointer to collect the memory
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
    while (current != nullptr) {
      Node *next = current->next;
      delete current;
      current = next;
    }
  }
};
} // namespace yao_lf
