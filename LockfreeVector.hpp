#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <new>
#include <optional>
#include <unistd.h>
#include <utility>

namespace yao_lf {
template <typename T> class LockFreeVector {
  using idx_pair = std::pair<std::size_t, std::size_t>;

private:
  std::atomic<std::atomic<T> *>
      ptr_arr_[32]{}; // at index i, it has 2^i capacity
  std::atomic<std::size_t> size_{0};

  static idx_pair get_idx(std::size_t g_idx) {
    std::size_t sz = g_idx + 1;
    std::size_t arr_idx = std::bit_width(sz) - 1;
    std::size_t cap = std::size_t{1} << arr_idx;
    std::size_t local_offset = sz ^ cap; // clears the highest bit
    return {arr_idx, local_offset};
  }

  std::atomic<T> *create_segment(std::size_t arr_idx) {
    if (arr_idx >= 32) {
      throw std::bad_alloc();
    }
    std::atomic<T> *segment = ptr_arr_[arr_idx].load(std::memory_order_acquire);
    if (segment != nullptr)
      return segment;
    auto *new_segment = new std::atomic<T>[std::size_t{1} << arr_idx] {};
    std::atomic<T> *expected = nullptr;
    if (ptr_arr_[arr_idx].compare_exchange_strong(expected, new_segment,
                                                  std::memory_order_release,
                                                  std::memory_order_acquire)) {
      return new_segment;
    }
    delete[] new_segment;
    return expected;
  }

public:
  LockFreeVector() = default;

  ~LockFreeVector() {
    for (std::size_t i = 0; i < 32; ++i) {
      delete[] ptr_arr_[i].load(std::memory_order_relaxed);
    }
  }

  LockFreeVector(const LockFreeVector &) = delete;

  LockFreeVector &operator=(const LockFreeVector &) = delete;

  void push_back(T &data) {
    std::size_t new_idx = size_.fetch_add(1);
    auto idx_p = get_idx(new_idx);

    if (idx_p.first >= 32) {
      throw std::bad_alloc();
    }

    create_segment(idx_p.first);

    std::atomic<T> *segment =
        ptr_arr_[idx_p.first].load(std::memory_order_acquire);

    segment[idx_p.second].store(data, std::memory_order_release);
  }
  void reserve(std::size_t size) {
    if (size == 0)
      return;
    auto [max_arr_idx, _] = get_idx(size - 1);
    if (max_arr_idx >= 32) {
      throw std::bad_alloc();
    }
    for (std::size_t i{}; i <= max_arr_idx; ++i) {
      create_segment(i);
    }
  }

  void pop_back() {
    std::size_t cur = size_.load(std::memory_order_relaxed);
    while (cur > 0 &&
           !size_.compare_exchange_weak(cur, cur - 1, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
    }
  }

  std::size_t capacity() const {
    int arr_idx{};
    std::size_t cap{};
    for (; arr_idx < 32 && ptr_arr_[arr_idx].load() != nullptr; arr_idx++) {
      cap += std::size_t{1} << arr_idx;
    }
    return cap;
  }

  std::optional<T> get(std::size_t q_idx) const {
    if (q_idx >= size_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }

    auto [arr_idx, local_offset] = get_idx(q_idx);
    if (arr_idx >= 32) {
      return std::nullopt;
    }

    std::atomic<T> *segment = ptr_arr_[arr_idx].load(std::memory_order_acquire);
    if (segment == nullptr) {
      // Writer reserved the index but hasn't attached the segment yet
      return std::nullopt;
    }

    return segment[local_offset].load(std::memory_order_acquire);
  }

  std::size_t size() { return size_.load(std::memory_order_relaxed); }
};
} // namespace yao_lf
