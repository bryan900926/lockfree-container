#include <cassert>
#include <thread>

#include "../hp/HpManager.hpp"
#include "test.hpp"

using namespace hp;
using namespace yao_test;
using namespace yao_mem;

struct DummyResource {
  std::atomic<int> &delete_count;

  DummyResource(std::atomic<int> &count) : delete_count(count) {}
  ~DummyResource() { delete_count.fetch_add(1, std::memory_order_relaxed); }
};

void dummy_deleter(void *ptr) {
  auto deleted_ptr = static_cast<DummyResource *>(ptr);
  delete deleted_ptr;
}

class HazardPointerTest : public Test {};

TEST(HazardPointerTest, Collection) {
  // Isolated counter for this specific test
  std::atomic<int> delete_count{0};

  // Create the resource
  DummyResource *resource = new DummyResource(delete_count);
  auto &hp_manager = HpManager<DummyResource>::instance();

  // ---------------------------------------------------------
  // STEP 1: Thread A reads the resource and sets a hazard pointer
  // ---------------------------------------------------------
  hp::ThreadHpOwner<DummyResource>::get_thread_hp_owner().protect(resource);

  // ---------------------------------------------------------
  // STEP 2: Thread B removes the resource from the main data
  // structure and retires it
  // ---------------------------------------------------------
  std::vector<RetiredNode> retired_list;
  RetiredNode node{};
  retired_list.push_back({resource, &dummy_deleter});

  hp_manager.adopt_orphans(std::move(retired_list));

  // ---------------------------------------------------------
  // STEP 3: Thread B attempts to clean up memory
  // ---------------------------------------------------------
  hp_manager.scan_orphans();

  // VERIFY: The resource is protected by the hazard pointer
  RELEASE_ASSERT(delete_count.load() == 0,
                 "Resource should NOT be deleted while HP is active!");

  // ---------------------------------------------------------
  // STEP 4: Thread A is done and releases the hazard pointer
  // ---------------------------------------------------------
  hp::ThreadHpOwner<DummyResource>::get_thread_hp_owner().clean();

  // ---------------------------------------------------------
  // STEP 5: Thread B attempts cleanup again
  // ---------------------------------------------------------
  hp_manager.scan_orphans();

  // VERIFY: The resource is now safely reclaimed
  RELEASE_ASSERT(delete_count.load() == 1, "resouce is not reclaimed\n");
}
TEST(HazardPointerTest, Dangling) {
  static const int THREAD_CNT = 200;

  // 1. Must use a raw pointer in std::atomic
  std::atomic<int *> shared_ptr{new int(0)};

  // WRITER: Continuously swaps the shared pointer with a new one
  auto writer_td = [&shared_ptr]() {
    const int num_op{100000};
    for (int i{}; i < num_op; ++i) {
      // Allocate a new raw Node
      int *new_ptr = new int(i);

      // 1. Get the current value
      int *old_ptr = shared_ptr.load(std::memory_order_relaxed);

      // 2. Try to swap it.
      while (!shared_ptr.compare_exchange_weak(old_ptr, new_ptr,
                                               std::memory_order_release,
                                               std::memory_order_relaxed)) {
      }

      // 3. We successfully swapped. Wrap old_ptr in unique_ptr to hand
      // ownership to HP.
      if (old_ptr) {
        // Assuming hp::retire takes std::unique_ptr<T> and your
        // StandardAllocator handles it
        hp::retire<int>(old_ptr);
      }
    }
  };

  // READER: Continuously tries to read the shared pointer
  auto reader_td = [&shared_ptr]() {
    const int num_op{100000};
    // Fix: Explicitly specify <Node<int>> for the owner
    hp::ThreadHpOwner<int> &owner =
        hp::ThreadHpOwner<int>::get_thread_hp_owner();

    for (int i{}; i < num_op; ++i) {
      int *safe_ptr = nullptr;

      // The core Hazard Pointer Read Pattern: Load -> Protect -> Verify
      do {
        safe_ptr = shared_ptr.load(std::memory_order_relaxed);

        // Protect the address we just loaded
        owner.protect(safe_ptr);

        // Verify it didn't change while we were protecting it.
      } while (safe_ptr != shared_ptr.load(std::memory_order_acquire));

      // Safely access data
      if (safe_ptr) {
        volatile int val = *safe_ptr;
        (void)val;
      }

      // Optional but good practice: release protection if doing heavy work
      // afterward owner.clear();
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
}
