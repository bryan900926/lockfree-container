cmake_minimum_required(VERSION 3.23)
project(LockFreeTest)


if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Debug' as none was specfied")
endif()

add_executable(test test_main.cpp test_hazard_pointer_collection.cpp)
