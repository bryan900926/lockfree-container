#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <vector>

#pragma once

namespace hp {

struct RetiredNode {
  void *ptr{nullptr};
  void (*deleter)(void *);
};

class HpManager {
public:
  struct HpRecord {
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
    auto it = nodes.begin();
    while (it != nodes.end()) {
      if (is_hazard(it->ptr)) {
        it++;
      } else {
        it->deleter(it->ptr);
        it = nodes.erase(it);
      }
    }
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
      node.deleter(node.ptr);
    }
    orphans_.clear();
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
      if (record.active.load() && record.ptr.load() == ptr)
        return true;
    }
    return false;
  }
};

class ThreadHpOwner {
public:
  static ThreadHpOwner &get_thread_hp_owner() {
    thread_local ThreadHpOwner hp_owner;
    return hp_owner;
  }
  ~ThreadHpOwner() { HpManager::instance().release_record(record_); }
  ThreadHpOwner &operator=(const ThreadHpOwner &) = delete;
  ThreadHpOwner(const ThreadHpOwner &) = delete;
  void protect(void *ptr) noexcept { record_->ptr.store(ptr); }
  void clear() noexcept { record_->ptr.store(nullptr); }

private:
  HpManager::HpRecord *record_ = nullptr;
  ThreadHpOwner() : record_(HpManager::instance().acquire_record()) {}
};

class ThreadRetiredList {
public:
  ~ThreadRetiredList() {
    collect();
    // If nodes_ still contain occupied pointer, transfer these nodes to the
    // manager to do the later cleaning
    HpManager::instance().adopt_orphans(std::move(nodes_));
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
  static const int SCAN_THRESHOLD = 64;
  void collect() {
    // First check any node is not used
    HpManager::instance().scan(nodes_);
    // Ask mananger to do the cleaning
    HpManager::instance().scan_orphans();
  }
};

template <typename T> void delete_as(void *ptr) {
  delete static_cast<T *>(ptr);
}

template <typename T> void retire(T *ptr) {
  ThreadRetiredList::get_thread_retired_list().push(
      RetiredNode{ptr, &delete_as<T>});
}
} // namespace hp
