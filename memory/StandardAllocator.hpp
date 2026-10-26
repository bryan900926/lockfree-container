#pragma once

namespace yao_mem {
template <typename T> struct StandardAllocator {

  using value_type = T;

  template <typename U> struct rebind {
    using other = yao_mem::StandardAllocator<U>;
  };

  [[nodiscard]] static T *allocate() { return new T; }
  static void disallocate(void *ptr) {
    auto deleted_ptr = static_cast<T *>(ptr);
    delete deleted_ptr;
  }
};
} // namespace yao_mem
