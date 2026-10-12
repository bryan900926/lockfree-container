#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace yao_lf {
template <typename T, size_t Capacity> class SPSCQueue {
  static_assert(std::atomic<size_t>::is_always_lock_free,
                "size_t is not always lock free :(");
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be 2^n !!");
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable");

private:
  std::array<T, Capacity> _buffer;
  alignas(64) std::atomic<size_t> _head;
  alignas(64) std::atomic<size_t> _tail;

public:
  SPSCQueue() : _head{}, _tail{} {}
  // only producer thread can use this method
  bool push(const T &data) {
    size_t cur_tail = _tail.load(std::memory_order_relaxed);
    size_t next_tail = (cur_tail + 1) & (Capacity - 1);
    if (next_tail == _head.load(std::memory_order_acquire)) {
      return false;
    }
    _buffer[cur_tail] = data; // copy into the array buffer
    _tail.store(next_tail, std::memory_order_release);
    return true;
  }
  // only consumer thread can use this method
  bool pop(T &out_data) {
    size_t cur_head = _head.load(std::memory_order_relaxed);
    if (cur_head == _tail.load(std::memory_order_acquire)) {
      return false;
    }
    out_data = _buffer[cur_head];
    size_t next_head = (cur_head + 1) & (Capacity - 1);
    _head.store(next_head, std::memory_order_release);
    return true;
  }
};
} // namespace yao_lf
