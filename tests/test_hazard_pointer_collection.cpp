#include <cassert>
#include <iostream>

#include "../hp/HpManager.hpp"
#include "../macro.hpp"

using namespace hp;

struct DummyResource {
  std::atomic<int> &delete_count;

  DummyResource(std::atomic<int> &count) : delete_count(count) {}
  ~DummyResource() { delete_count.fetch_add(1, std::memory_order_relaxed); }
};

void dummy_deleter(void *ptr) { delete static_cast<DummyResource *>(ptr); }

void test_hazard_pointer_collection() {
  // Isolated counter for this specific test
  std::atomic<int> delete_count{0};

  // Create the resource
  auto *resource = new DummyResource(delete_count);
  auto &hp_manager = HpManager::instance();

  // ---------------------------------------------------------
  // STEP 1: Thread A reads the resource and sets a hazard pointer
  // ---------------------------------------------------------
  auto *record = hp_manager.acquire_record();
  hp::ThreadHpOwner::get_thread_hp_owner().protect(resource);

  // ---------------------------------------------------------
  // STEP 2: Thread B removes the resource from the main data
  // structure and retires it
  // ---------------------------------------------------------
  std::vector<RetiredNode> retired_list;
  retired_list.push_back({resource, dummy_deleter});
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
  hp_manager.release_record(record);

  // ---------------------------------------------------------
  // STEP 5: Thread B attempts cleanup again
  // ---------------------------------------------------------
  hp_manager.scan_orphans();

  // VERIFY: The resource is now safely reclaimed
  RELEASE_ASSERT(delete_count.load() == 1,
                 "[Fail] test_hazard_pointer_collection()\n");

  std::cerr << "[Success] test_hazard_pointer_collection()\n";
}
