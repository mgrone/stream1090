cmake_minimum_required(VERSION 3.10)
project(stream1090)

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE "Release")
   message(STATUS "Build type not specified: defaulting to release.")
endif(NOT CMAKE_BUILD_TYPE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-Wall -Wextra -Wunused-function -O3 -DSTATS_ENABLED)
add_executable(stream1090 main.cpp)
target_include_directories(stream1090 PRIVATE include)

add_executable(table_gen table_gen.cpp)
target_include_directories(table_gen PRIVATE include)
