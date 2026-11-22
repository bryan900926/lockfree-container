#include "../LockFreeVector.hpp"
#include "test.hpp"
#include <thread>

using namespace yao_test;

class LfVectorTest : public Test {};

TEST(LfVectorTest, Push, true) {
  const int MX_OP = 1000000;
  const int num_of_thread = 20;

  RELEASE_ASSERT(MX_OP % num_of_thread == 0,
                 "MX_OP must dividable by num of threads");

  yao_lf::LockFreeVector<int> vec;
  std::atomic<bool> start{false};

  auto push_fun = [&]() -> void {
    const int op = MX_OP / num_of_thread;
    while (!start.load())
      std::this_thread::yield();
    for (int i{}; i < op; ++i) {
      vec.push_back(i);
    }
  };
  std::vector<std::thread> thread_vec;
  for (int i{}; i < num_of_thread; ++i) {
    thread_vec.push_back(std::thread(push_fun));
  }
  start.store(true);
  for (auto &t : thread_vec)
    t.join();
  RELEASE_ASSERT(vec.size() == MX_OP, "element might fail to insert");
}

TEST(LfVectorTest, Reserve, true) {
  // reserve the two to the power
  yao_lf::LockFreeVector<int> vec;
  /*
   *  1 + 2 + .. 2^n -> 2^(n + 1) - 1
   */
  std::size_t cap = std::size_t{1} << 20;
  vec.reserve(cap);
  RELEASE_ASSERT(vec.capacity() == (cap << 1) - 1, "reserve size mismatch");
  // reserve other size
  std::size_t cap1 = std::size_t{1} << 20;
  yao_lf::LockFreeVector<int> vec1;
  vec1.reserve(cap1 + 4);
  RELEASE_ASSERT(vec1.capacity() == (cap1 << 1) - 1, "reserve size mismatch");
}
