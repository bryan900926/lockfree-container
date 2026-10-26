#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct BenchmarkInfo {
  std::string name;
  std::function<void()> run_fn;
  bool enabled;
};

class BenchmarkRegistry {
public:
  static BenchmarkRegistry &get() {
    static BenchmarkRegistry instance;
    return instance;
  }

  void register_benchmark(const char *name, std::function<void()> fn,
                          bool enabled) {
    benchmarks.push_back({name, fn, enabled});
  }

  void run_all() {
    for (const auto &bench : benchmarks) {
      if (!bench.enabled) {
        continue;
      }

      std::cout << "[ RUN      ] " << bench.name << "\n";
      auto start = std::chrono::high_resolution_clock::now();

      bench.run_fn(); // Execute the actual benchmark

      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double, std::milli> ms = end - start;
      std::cout << "[       OK ] " << bench.name << " (" << ms.count()
                << " ms)\n";
    }
  }

private:
  std::vector<BenchmarkInfo> benchmarks;
};

#define BENCHMARK(Name, Enabled)                                               \
  void Name##_fn(); /* Forward declare the user's function */                  \
  struct Name##_registrar {                                                    \
    Name##_registrar() {                                                       \
      BenchmarkRegistry::get().register_benchmark(#Name, Name##_fn, Enabled);  \
    }                                                                          \
  } Name##_instance; /* Global instance triggers constructor before main */    \
  void Name##_fn()   /* Start the function body for the user */
