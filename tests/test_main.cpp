#include <cassert>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "../SPSCQueue.hpp"
#include "../macro.hpp"

using namespace yao_lf;

void test_unbounded_spsc();
void test_hazard_pointer_collection();
void test_hazard_pointer_dangling();
void test_trieber_stack();

int main() {
  test_unbounded_spsc();
  test_hazard_pointer_dangling();
  test_hazard_pointer_collection();
  test_trieber_stack();
  return EXIT_SUCCESS;
}
