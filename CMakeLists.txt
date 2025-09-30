cmake_minimum_required(VERSION 3.10)
project(stream1090)

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE "Release")
   message(STATUS "Build type not specified: defaulting to release.")
endif(NOT CMAKE_BUILD_TYPE)

option(ENABLE_STATS "Compile with STATS_ENABLED (1 = enabled, 0 = disabled)" ON)
option(ENABLE_OUTPUT_RAW "Compile with OUTPUT_RAW (1 = enabled, 0 = disabled)" OFF)

if(ENABLE_STATS)
  set(STATS_DEF STATS_ENABLED=1)
else()
  set(STATS_DEF STATS_ENABLED=0)
endif()

if(ENABLE_OUTPUT_RAW)
  set(OUTPUT_DEF OUTPUT_RAW=1)
else()
  set(OUTPUT_DEF OUTPUT_RAW=0)
endif()


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DEFAULT_COMPILE_OPTIONS -Wall -Wextra -Wunused-function -O3)

add_executable(stream1090 main.cpp)
target_include_directories(stream1090 PRIVATE include)
target_compile_options(stream1090 PRIVATE ${DEFAULT_COMPILE_OPTIONS})
target_compile_definitions(stream1090 PRIVATE ${STATS_DEF} ${OUTPUT_DEF})
target_compile_features(stream1090 PRIVATE cxx_std_20)


add_executable(table_gen table_gen.cpp)
target_include_directories(table_gen PRIVATE include)
target_compile_options(table_gen PRIVATE ${DEFAULT_COMPILE_OPTIONS})
target_compile_features(table_gen PRIVATE cxx_std_20)
set_target_properties(table_gen PROPERTIES EXCLUDE_FROM_ALL TRUE)