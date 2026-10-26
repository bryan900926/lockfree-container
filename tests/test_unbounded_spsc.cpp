#include <thread>

#include "../SPSCQueue.hpp"

#include "test.hpp"

using namespace yao_lf;
using namespace yao_test;

class SPSCQueueTest : public Test {};

TEST(SPSCQueueTest, Contention) {
  const int ITEM_COUNT = 1'000'000;
  SPSCQueue<int, 1 << 10> spsc_q;

  std::thread c_thread([&]() {
    int expected{};
    while (expected < ITEM_COUNT) {
      int val;
      if (spsc_q.pop(val)) {
        expected++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  std::thread p_thread([&]() {
    for (int i{}; i < ITEM_COUNT; ++i) {
      spsc_q.push(i);
    }
  });

  p_thread.join();
  c_thread.join();
}
