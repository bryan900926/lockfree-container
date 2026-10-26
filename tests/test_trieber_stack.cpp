#include <cassert>
#include <thread>
#include <vector>

#include "../TreiberStack.hpp"
#include "../memory/FreeListAllocator.hpp"
#include "test.hpp"

struct Dummydata {
  int i1 = -1;
  float f2 = -1;
  long l3 = -1;
};

using namespace yao_test;

class TrieberTest : public Test {};

TEST(TrieberTest, Contention) {
  using namespace yao_lf;
  TrieberStack<Dummydata, yao_mem::FreeListAllocator<Dummydata>> stk;

  const int write_op{100000};
  const int read_op{100000};

  // same number of writers and readers to test if element correctly poped out
  const int num_writer_threads{10};
  const int num_reader_threads{num_writer_threads};

  std::vector<std::thread> readers;
  std::vector<std::thread> writers;

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
  // stack should be empty since same writer and reader
  RELEASE_ASSERT(!stk.pop(data), "stack is not empty!!");
}
