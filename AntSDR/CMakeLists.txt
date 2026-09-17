cmake_minimum_required(VERSION 3.10)
project(stream1090_embedded VERSION 1.0.0)

# Set build type to Release for optimal optimization
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

# Hardcoded feature flags optimized exactly for the AntSDR E200
set(STATS_DEF              "STATS_ENABLED=1")
set(STATS_END_DEF          "STATS_END_ONLY=0")
set(CUSTOM_INPUT_DEF       "STREAM1090_CUSTOM_INPUT=1") # Forces input feed mode
set(RSSI_DEF               "STREAM1090_RSSI=1")
set(PREAMBLE_GATE_DEF      "STREAM1090_PREAMBLE_GATE=1")
set(PREAMBLE_THRESHOLD_DEF "STREAM1090_PREAMBLE_THRESHOLD=1.2")
set(MIN_SNR_DEF            "STREAM1090_MIN_SNR=2.5")
set(TOO_MUCH_CPU_DEF       "STREAM1090_TOO_MUCH_CPU=0")

# Compiler settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags (-pthread is vital for modern static C++ threading)
set(DEFAULT_COMPILE_OPTIONS
    -Wall -Wextra -Wunused-function -O3 -ffast-math -pthread
)

# Core source resolution (referencing the parent directory)
file(GLOB STREAM1090_CORE ../src/*.cpp)

# Main executable target configuration
add_executable(stream1090
    ../main.cpp
    ${STREAM1090_CORE}
)

# Reference the parent include directory 
target_include_directories(stream1090 PRIVATE
    ../include
)

target_compile_options(stream1090 PRIVATE ${DEFAULT_COMPILE_OPTIONS})

target_compile_definitions(stream1090 PRIVATE
    ${STATS_DEF}
    ${STATS_END_DEF}
    ${CUSTOM_INPUT_DEF}
    ${RSSI_DEF}
    ${TOO_MUCH_CPU_DEF}
    ${PREAMBLE_GATE_DEF}
    ${PREAMBLE_THRESHOLD_DEF}
    ${MIN_SNR_DEF}
)

# Force pthread linkage for runtime stability
target_link_libraries(stream1090 PRIVATE -pthread)
