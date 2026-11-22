#include <cstdlib>

#define YAO_BENCH
#include "benchmark.hpp"

int main(void) {
  BenchmarkRegistry::get().run_all();
  return EXIT_SUCCESS;
}
