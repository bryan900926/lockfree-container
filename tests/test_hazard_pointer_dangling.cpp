#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include "../hp/HpManager.hpp"

void test_hazard_pointer_dangling() {
  static const int THREAD_CNT = 200;

  // The shared resource that threads will contend for
  std::atomic<int *> shared_ptr{new int(0)};

  // WRITER: Continuously swaps the shared pointer with a new one
  auto writer_td = [&shared_ptr]() {
    const int num_op{100000};
    for (int i{}; i < num_op; ++i) {
      int *new_ptr = new int(i);

      // 1. Get the current value
      int *old_ptr = shared_ptr.load();

      // 2. Try to swap it. If it fails, old_ptr is automatically updated.
      // We loop WHILE it returns false (meaning the swap failed).
      while (!shared_ptr.compare_exchange_weak(old_ptr, new_ptr)) {
      }

      // 3. We successfully swapped. The old_ptr is now disconnected from
      // shared_ptr. It is safe to retire it. The HP framework will wait to
      // delete it until no readers are protecting it.
      if (old_ptr) {
        hp::retire(old_ptr);
      }
    }
  };

  // READER: Continuously tries to read the shared pointer
  auto reader_td = [&shared_ptr]() {
    const int num_op{100000};
    hp::ThreadHpOwner &owner = hp::ThreadHpOwner::get_thread_hp_owner();

    for (int i{}; i < num_op; ++i) {
      int *safe_ptr = nullptr;

      // The core Hazard Pointer Read Pattern: Load -> Protect -> Verify
      do {
        safe_ptr = shared_ptr.load(std::memory_order_relaxed);

        // Protect the address we just loaded
        owner.protect(safe_ptr);

        // Verify it didn't change while we were protecting it.
        // If it did, loop around and try again.
      } while (safe_ptr != shared_ptr.load(std::memory_order_acquire));

      // If your HP framework is broken, the next line will sporadically
      // segfault (use-after-free) because the writer thread would have already
      // deleted it.
      if (safe_ptr) {
        volatile int val = *safe_ptr;
        (void)val;
      }
    }
  };

  // Spawn threads: 2 writers, 18 readers
  std::vector<std::thread> threads;
  for (int i = 0; i < 2; ++i) {
    threads.push_back(std::thread(writer_td));
  }
  for (int i = 0; i < THREAD_CNT - 2; ++i) {
    threads.push_back(std::thread(reader_td));
  }

  // Wait for all threads to finish
  for (auto &th : threads) {
    th.join();
  }

  // Clean up the very last pointer left in the atomic after all threads stop
  int *final_ptr = shared_ptr.load();
  if (final_ptr) {
    delete final_ptr;
  }

  std::cout << "[Success] run_hazard_ptr_release_resource()\n";
}
