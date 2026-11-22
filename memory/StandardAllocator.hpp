#pragma once

#include <cstddef>
namespace yao_mem {
template <typename T> struct StandardAllocator {

  using value_type = T;

  template <typename U> struct rebind {
    using other = yao_mem::StandardAllocator<U>;
  };

  [[nodiscard]] static T *allocate(std::size_t n = 1) {
    return static_cast<T *>(::operator new(n * sizeof(T)));
  }

  static void deallocate(T *ptr, std::size_t n = 1) {
    if (!ptr)
      return;
    delete ptr;
  }

  // since std allocator not have thread local state, it just use same free
  // method
  void static fall_back_deleter(T *ptr) {
    if (!ptr)
      return;
    delete ptr;
  }
};
} // namespace yao_mem
