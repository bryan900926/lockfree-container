#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

class BenchmarkState {
public:
  BenchmarkState() { resume_timing(); }

  void pause_timing() {
    if (running_) {
      auto now = std::chrono::high_resolution_clock::now();
      elapsed_ += (now - start_time_);
      running_ = false;
    }
  }

  void resume_timing() {
    if (!running_) {
      start_time_ = std::chrono::high_resolution_clock::now();
      running_ = true;
    }
  }

  double get_elapsed_ms() {
    if (running_) {
      pause_timing();
    }
    return std::chrono::duration<double, std::milli>(elapsed_).count();
  }

private:
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
  std::chrono::high_resolution_clock::duration elapsed_{0};
  bool running_ = false;
};

struct BenchmarkInfo {
  std::string name;
  std::function<void(BenchmarkState &)> run_fn;
  bool enabled;
};

class BenchmarkRegistry {
public:
  static BenchmarkRegistry &get() {
    static BenchmarkRegistry instance;
    return instance;
  }

  void register_benchmark(const char *name,
                          std::function<void(BenchmarkState &)> fn,
                          bool enabled) {
    benchmarks.push_back({name, fn, enabled});
  }

  void run_all() {
    for (const auto &bench : benchmarks) {
      if (!bench.enabled) {
        continue;
      }

      std::cout << "[ RUN      ] " << bench.name << "\n";
      BenchmarkState bm_state;

      bench.run_fn(bm_state); // Execute the actual benchmark

      std::cout << "[       OK ] " << bench.name << " ("
                << bm_state.get_elapsed_ms() << " ms)\n";
    }
  }

private:
  std::vector<BenchmarkInfo> benchmarks;
};

#define BENCHMARK(Name, Enabled)                                               \
  void Name##_fn(                                                              \
      BenchmarkState &state); /* Forward declare the user's function */        \
  struct Name##_registrar {                                                    \
    Name##_registrar() {                                                       \
      BenchmarkRegistry::get().register_benchmark(#Name, Name##_fn, Enabled);  \
    }                                                                          \
  } Name##_instance; /* Global instance triggers constructor before main */    \
  void Name##_fn(                                                              \
      BenchmarkState &state) /* Start the function body for the user */
