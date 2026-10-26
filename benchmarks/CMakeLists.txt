 
cmake_minimum_required(VERSION 3.23)
project(LockFreeBenchmark)


if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specfied")
endif()

add_executable(bm bm_main.cpp bm_trieber_stack.cpp)

# Apply compiler flags ONLY in Debug mode
target_compile_options(bm PRIVATE
    $<$<CONFIG:Release>: -O3 >
)


