#include <thread>

#include "../TreiberStack.hpp"
#include "../memory/FreeListAllocator.hpp"
#include "../memory/StandardAllocator.hpp"
#include "benchmark.hpp"

BENCHMARK(TrieberStack_std_allocator, true) {
  using namespace yao_lf;
  TrieberStack<int, yao_mem::StandardAllocator<int>> stk;

  const int write_op{1000000};
  const int read_op{1000000};

  // same number of writers and readers to test if element correctly poped out
  const int num_writer_threads{10};
  const int num_reader_threads{num_writer_threads};

  std::vector<std::thread> readers;
  std::vector<std::thread> writers;

  // 2. Spawn Reader Threads
  for (int i = 0; i < num_reader_threads; ++i) {
    readers.emplace_back([&]() {
      for (int j = 0; j < read_op; ++j) {
        int data;

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
            int data{j};
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
  int data;
}
BENCHMARK(TrieberStack_freeList_allocator, true) {
  using namespace yao_lf;
  TrieberStack<int, yao_mem::FreeListAllocator<int, 4096>> stk;

  const int write_op{1000000};
  const int read_op{1000000};

  // same number of writers and readers to test if element correctly poped out
  const int num_writer_threads{10};
  const int num_reader_threads{num_writer_threads};

  std::vector<std::thread> readers;
  std::vector<std::thread> writers;

  // 2. Spawn Reader Threads
  for (int i = 0; i < num_reader_threads; ++i) {
    readers.emplace_back([&]() {
      for (int j = 0; j < read_op; ++j) {
        int data;

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
            int data{j};
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
  int data;
}
