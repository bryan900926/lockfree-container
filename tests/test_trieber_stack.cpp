#include "../TreiberStack.hpp"
#include <cassert>
#include <iostream>
#include <ostream>
#include <thread>
#include <vector>

struct Dummydata {
  int i1 = -1;
  float f2 = -1;
  long l3 = -1;
};

void test_trieber_stack() {
  using namespace yao_lf;
  TrieberStack<Dummydata> stk;

  const int write_op{100000};
  const int read_op{100000};

  // same number of writers and readers to test if element correctly poped out
  const int num_writer_threads{10};
  const int num_reader_threads{num_writer_threads};

  std::vector<std::thread> readers;
  std::vector<std::thread> writers;

  // 1. Spawn Writer Threads
  for (int i = 0; i < num_writer_threads; ++i) {
    writers.emplace_back(
        [&, i]() { // capture 'i' by value for thread ID if needed
          for (int j = 0; j < write_op; ++j) {
            Dummydata data = {.i1 = j, .f2 = 9, .l3 = 6};
            stk.push(data);
          }
        });
  }

  // 2. Spawn Reader Threads
  for (int i = 0; i < num_reader_threads; ++i) {
    readers.emplace_back([&]() {
      for (int j = 0; j < read_op; ++j) {
        Dummydata data;

        // NOTE: If stk.pop() returns void and simply exits when the stack is
        // empty, this loop might burn through its 10000 iterations instantly if
        // the reader threads start faster than the writer threads.
        while (!stk.pop(data))
          std::this_thread::yield();
      }
    });
  }

  // 3. Wait for all writers to finish
  for (auto &t : writers) {
    if (t.joinable()) {
      t.join();
    }
  }

  // 4. Wait for all readers to finish
  for (auto &t : readers) {
    if (t.joinable()) {
      t.join();
    }
  }
  Dummydata data;
  bool f = stk.pop(data);
  if (f) {
    std::cout << "[Fail] test_trieber_stack()\n";
    return;
  }
  assert(!f && "[Fail] test_trieber_stack()\n");
  std::cout << "[Success] test_trieber_stack()\n";
}
