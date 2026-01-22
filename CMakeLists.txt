cmake_minimum_required(VERSION 3.10)
project(stream1090)

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE "Release")
   message(STATUS "Build type not specified: defaulting to release.")
endif(NOT CMAKE_BUILD_TYPE)

option(ENABLE_STATS "Compile with STATS_ENABLED (1 = enabled, 0 = disabled)" ON)
option(END_STATS "Compile with STATS_END_ONLY (1 = enabled, 0 = disabled)" OFF)
option(ENABLE_OUTPUT_RAW "Compile with OUTPUT_RAW (1 = enabled, 0 = disabled)" OFF)


if(ENABLE_STATS)
  set(STATS_DEF STATS_ENABLED=1)
else()
  set(STATS_DEF STATS_ENABLED=0)
endif()

if(END_STATS)
  set(STATS_END_DEF STATS_END_ONLY=1)
else()
  set(STATS_END_DEF STATS_END_ONLY=0)
endif()

if(ENABLE_OUTPUT_RAW)
  set(OUTPUT_DEF OUTPUT_RAW=1)
else()
  set(OUTPUT_DEF OUTPUT_RAW=0)
endif()


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DEFAULT_COMPILE_OPTIONS -Wall -Wextra -Wunused-function -O3 -ffast-math)

# 0 corresponds to the original rtl_sdr 2.4 Mhz
set(SAMPLE_RATES_MHZ 0 6 10)

set(SRC_FILES main.cpp) 
set(INCLUDE_DIRS include)

function(mhz_to_suffix_and_hz mhz out_suffix out_hz)
    set(${out_suffix} "${mhz}M" PARENT_SCOPE)
    math(EXPR hz "${mhz} * 1000000")
    set(${out_hz} "${hz}" PARENT_SCOPE)
endfunction()

# Create a target for each sample rate
foreach(MHZ IN LISTS SAMPLE_RATES_MHZ)
    mhz_to_suffix_and_hz(${MHZ} SUFFIX HZ)

    if(MHZ EQUAL 0)
      set(TGT_NAME stream1090)
    else()
      set(TGT_NAME stream1090_${SUFFIX})
    endif()
    
    add_executable(${TGT_NAME} ${SRC_FILES})
    target_include_directories(${TGT_NAME} PRIVATE ${INCLUDE_DIRS})
    target_compile_options(${TGT_NAME} PRIVATE ${DEFAULT_COMPILE_OPTIONS})
    target_compile_definitions(${TGT_NAME} PRIVATE ${STATS_DEF} ${STATS_END_DEF} ${OUTPUT_DEF})
    target_compile_definitions(${TGT_NAME} PRIVATE COMPILER_INPUT_SAMPLE_RATE=${HZ})
    target_compile_features(${TGT_NAME} PRIVATE cxx_std_20)
endforeach()

add_executable(table_gen table_gen.cpp)
target_include_directories(table_gen PRIVATE include)
target_compile_options(table_gen PRIVATE ${DEFAULT_COMPILE_OPTIONS})
target_compile_features(table_gen PRIVATE cxx_std_20)
set_target_properties(table_gen PROPERTIES EXCLUDE_FROM_ALL TRUE)
