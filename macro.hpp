#pragma once

#define RELEASE_ASSERT(condition, message)                                     \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Assertion failed: (" #condition ")\n"                      \
                << "Message: " << message << "\n"                              \
                << "File: " << __FILE__ << ":" << __LINE__ << "\n"             \
                << "Function: " << __func__ << "\n";                           \
      std::abort();                                                            \
    }                                                                          \
  } while (false)
