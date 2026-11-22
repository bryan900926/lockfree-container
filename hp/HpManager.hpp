#include "../memory/StandardAllocator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <vector>

#pragma once

namespace hp {

struct RetiredNode {
  void *ptr{nullptr};
  void (*deleter)(void *);
  void (*fallback_deleter)(void *){nullptr};
};

template <typename T> class HpManager {
public:
  struct alignas(64) HpRecord {
    std::atomic<bool> active{false};
    std::atomic<void *> ptr{nullptr};
  };
  HpManager(HpManager &&other) = delete;
  HpManager(const HpManager &other) = delete;
  HpManager &operator=(const HpManager &other) = delete;
  HpManager &operator=(const HpManager &&other) = delete;

  static HpManager &instance() {
    static HpManager hp_manager;
    return hp_manager;
  }

  HpRecord *acquire_record() {
    for (auto &record : records_) {
      bool expected{false};
      // We use acquired barrier to ensure it flush the old cache to sync with
      // release_reocrd
      if (record.active.compare_exchange_strong(expected, true,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed)) {
        return &record;
      }
    }
    throw std::runtime_error("Hazard pointer is not enough!!");
  }

  void release_record(HpRecord *record) noexcept {
    // null pointer means someone cleared it
    if (!record->ptr.load(std::memory_order_relaxed)) {
      return;
    }
    record->ptr.store(nullptr, std::memory_order_relaxed);
    record->active.store(
        false,
        std::memory_order_release); // ensure ptr.store(null) happen before
  }

  void scan_orphans() {
    std::vector<RetiredNode> local;
    {
      std::lock_guard<std::mutex> lk(orphan_mutex_);
      local.swap(orphans_); // transfer the current orphans to the local to do
                            // the expensive processing
    }
    scan(local);
    if (!local.empty()) {
      std::lock_guard<std::mutex> lk(orphan_mutex_);
      orphans_.insert(orphans_.end(), local.begin(), local.end());
    }
  }

  void scan(std::vector<RetiredNode> &nodes) const {
    // 1. Gather active hazard pointers ONCE to avoid cache-line bouncing
    std::vector<void *> active_hps;
    active_hps.reserve(records_.size());
    for (const auto &record : records_) {
      if (record.active.load(std::memory_order_acquire)) {
        if (void *ptr = record.ptr.load(std::memory_order_acquire); ptr) {
          active_hps.push_back(ptr);
        }
      }
    }

    std::sort(active_hps.begin(), active_hps.end());

    // 3. Filter nodes in a single O(N) pass without erasing/shifting memory
    std::vector<RetiredNode> surviving_nodes;
    surviving_nodes.reserve(nodes.size());

    for (auto &node : nodes) {
      if (std::binary_search(active_hps.begin(), active_hps.end(), node.ptr)) {
        surviving_nodes.push_back(node); // Pointer still in use
      } else {
        node.deleter(node.ptr); // Safe to delete!
      }
    }

    // 4. Replace the old list with the surviving nodes
    nodes = std::move(surviving_nodes);
  }

  void adopt_orphans(std::vector<RetiredNode> &&nodes) {
    if (nodes.empty())
      return;
    std::lock_guard<std::mutex> lk(orphan_mutex_);
    orphans_.insert(orphans_.end(), nodes.begin(), nodes.end());
  }

  ~HpManager() {
    std::lock_guard<std::mutex> lk(orphan_mutex_);
    for (auto &node : orphans_) {
      node.fallback_deleter(node.ptr);
    }
  }

private:
  HpManager() = default;
  constexpr static int MAX_HAZARD_PTR_ = 512;
  std::array<HpRecord, MAX_HAZARD_PTR_> records_;
  std::vector<RetiredNode> orphans_;
  std::mutex orphan_mutex_; // guard different threads might use scan_orphans()
                            // and adopt_orphans() at the same time
  bool is_hazard(void *ptr) const noexcept {
    if (!ptr) {
      return false;
    }
    for (const auto &record : records_) {
      if (record.active.load(std::memory_order_relaxed) &&
          record.ptr.load(std::memory_order_relaxed) == ptr)
        return true;
    }
    return false;
  }
};

template <typename T> class ThreadHpOwner {
public:
  static ThreadHpOwner &get_thread_hp_owner() {
    thread_local ThreadHpOwner hp_owner;
    return hp_owner;
  }
  ~ThreadHpOwner() { clean(); }
  ThreadHpOwner &operator=(const ThreadHpOwner &) = delete;
  ThreadHpOwner(const ThreadHpOwner &) = delete;
  void protect(void *ptr) noexcept { record_->ptr.store(ptr); }
  void clear() noexcept { record_->ptr.store(nullptr); }
  void clean() {
    HpManager<T>::instance().release_record(record_);
    clear();
  }

private:
  typename HpManager<T>::HpRecord *record_ = nullptr;
  ThreadHpOwner() : record_(HpManager<T>::instance().acquire_record()) {}
};

template <typename T> class ThreadRetiredList {
public:
  ~ThreadRetiredList() {
    // If nodes_ still contain occupied pointer, transfer these nodes to the
    // manager to do the later cleaning
    HpManager<T>::instance().adopt_orphans(std::move(nodes_));
  }
  void push(RetiredNode node) {
    nodes_.push_back(node);
    if (nodes_.size() >= SCAN_THRESHOLD) {
      collect();
    }
  }
  static ThreadRetiredList &get_thread_retired_list() {
    thread_local ThreadRetiredList lst;
    return lst;
  }

private:
  std::vector<RetiredNode> nodes_;
  static const int SCAN_THRESHOLD = 128;
  void collect() {
    // First check any node is not used
    HpManager<T>::instance().scan(nodes_);
    // Ask mananger to do the cleaning
    HpManager<T>::instance().scan_orphans();
  }
};

template <typename T, typename Allocator = yao_mem::StandardAllocator<T>>
void retire(void *ptr) {
  RetiredNode r_node;
  r_node.ptr = ptr;

  // A captureless lambda decays to a standard function pointer: void(*)(void*)
  r_node.deleter = [](void *p) {
    T *typed_ptr = static_cast<T *>(p);
    Allocator alloc;

    // 1. Destroy the object (call destructor)
    std::allocator_traits<Allocator>::destroy(alloc, typed_ptr);

    // 2. Deallocate memory (pass 1 as the count)
    std::allocator_traits<Allocator>::deallocate(alloc, typed_ptr, 1);
  };

  r_node.fallback_deleter = [](void *p) {
    T *typed_ptr = static_cast<T *>(p);
    Allocator::fall_back_deleter(typed_ptr);
  };

  ThreadRetiredList<T>::get_thread_retired_list().push(r_node);
}
} // namespace hp
