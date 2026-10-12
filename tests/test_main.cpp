#include <cassert>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "../SPSCQueue.hpp"

using namespace yao_lf;

void run_spsc_test();

int main() {
  run_spsc_test();
  return EXIT_SUCCESS;
}

void run_spsc_test() {
  const int ITEM_COUNT = 1'000'000;
  SPSCQueue<int, 1 << 20> spsc_q;

  std::thread p_thread([&]() {
    for (int i{}; i < ITEM_COUNT; ++i) {
      while (!spsc_q.push(i))
        std::this_thread::yield();
    }
  });

  std::thread c_thread([&]() {
    int expected{};
    while (expected < ITEM_COUNT) {
      int val;
      if (spsc_q.pop(val)) {
        assert(val == expected && "[Fail] run_spsc_test()");
        expected++;
      } else {
        std::this_thread::yield();
      }
    }
  });
  p_thread.join();
  c_thread.join();
  std::cout << "[Sucess] run_spsc_test()\n";
}
