#pragma once

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"

namespace yao_test {
#define RELEASE_ASSERT(condition, message)                                     \
  do {                                                                         \
    if (!(condition)) {                                                        \
      throw std::runtime_error(                                                \
          std::string("Assertion failed: (" #condition ")\n") +                \
          "Message: " + std::string(message) + "\n" + "File: " + __FILE__ +    \
          ":" + std::to_string(__LINE__) + "\n" + "Function: " + __func__);    \
    }                                                                          \
  } while (0)

struct Test {
  virtual ~Test() {}
  virtual void set_up() {}
  virtual void tear_down() {}
  virtual void run() = 0;
};

using TestFactory = Test *(*)();

struct TestCase {
  std::string name;
  TestFactory factory;
};

inline std::vector<TestCase> &get_test_registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct TestRegister {
  TestRegister(const char *name, TestFactory factory, bool is_run) {
    if (is_run)
      get_test_registry().push_back({name, factory});
  }
};

#define TEST(FixtureName, TestName, is_run)                                    \
  static_assert(std::is_same<decltype(is_run), bool>::value,                   \
                "TEST macro error: The 'is_run' argument must strictly be a "  \
                "bool (true or false).");                                      \
  class FixtureName##_##TestName : public FixtureName {                        \
  public:                                                                      \
    void run() override;                                                       \
  };                                                                           \
  Test *Factory_##FixtureName##_##TestName() {                                 \
    return new FixtureName##_##TestName();                                     \
  }                                                                            \
  static TestRegister register_##FixtureName##_##TestName(                     \
      #FixtureName "." #TestName, &Factory_##FixtureName##_##TestName,         \
      is_run);                                                                 \
  void FixtureName##_##TestName::run()

inline void test_all() {
  const auto &tests = get_test_registry();
  const int tot_test = tests.size();

  std::cout << GREEN << "Running: " << RESET << tot_test << " tests\n\n";

  // 1. Fixed the loop increment
  for (int i = 0; i < tot_test; ++i) {
    // 2. Fixed array access using 'tests[i]'
    Test *instance = tests[i].factory();
    std::cout << YELLOW << "[Ruuing]: " << RESET << tests[i].name << "\n";
    try {
      instance->set_up();
      instance->run();
      // Optional: Print success if it doesn't throw
      std::cout << GREEN << "[Pass]: " << RESET << tests[i].name << "\n";

    } catch (const std::exception &ex) { // 3. Catch by const reference
      // 4. Fixed syntax, added exception message and color reset
      std::cout << RED << "[Fail]: " << RESET << tests[i].name << " - "
                << ex.what() << "\n\n";
    }

    std::cout << "\n";

    instance->tear_down();
    delete instance;
  }
}
} // namespace yao_test
