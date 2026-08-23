cmake_minimum_required(VERSION 3.16)
project(TinyKV CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
