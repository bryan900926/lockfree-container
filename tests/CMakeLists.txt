cmake_minimum_required(VERSION 3.23)
project(LockFreeTest)


if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Debug' as none was specfied")
endif()

add_executable(test test_main.cpp test_hazard_pointer.cpp 
test_trieber_stack.cpp test_unbounded_spsc.cpp)

# Apply compiler flags ONLY in Debug mode
target_compile_options(test PRIVATE
    $<$<CONFIG:Debug>:-g -O0 -fsanitize=address -fno-omit-frame-pointer>
)

# ASAN also requires linker flags
target_link_options(test PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
)

