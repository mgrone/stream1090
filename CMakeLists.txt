cmake_minimum_required(VERSION 3.10)
project(stream1090)

# ------------------------------------------------------------
# Build type
# ------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    message(STATUS "[stream1090] Build type not specified: defaulting to Release")
endif()

# ------------------------------------------------------------
# Options
# ------------------------------------------------------------
option(ENABLE_STATS      "Compile with STATS_ENABLED" ON)
option(END_STATS         "Compile with STATS_END_ONLY" OFF)
option(ENABLE_OUTPUT_RAW "Compile with OUTPUT_RAW" OFF)
option(ENABLE_CUSTOM_INPUT "Compile with STREAM1090_CUSTOM_INPUT" OFF)

set(STATS_DEF        STATS_ENABLED=$<BOOL:${ENABLE_STATS}>)
set(STATS_END_DEF    STATS_END_ONLY=$<BOOL:${END_STATS}>)
set(OUTPUT_DEF       OUTPUT_RAW=$<BOOL:${ENABLE_OUTPUT_RAW}>)
set(CUSTOM_INPUT_DEF STREAM1090_CUSTOM_INPUT=$<BOOL:${ENABLE_CUSTOM_INPUT}>)

# ------------------------------------------------------------
# Compiler settings
# ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DEFAULT_COMPILE_OPTIONS
    -Wall -Wextra -Wunused-function -O3 -ffast-math
)

# ------------------------------------------------------------
# Device backend detection
# ------------------------------------------------------------
find_package(PkgConfig REQUIRED)

set(DEVICE_SOURCES "")
set(DEVICE_LIBS "")
set(DEVICE_INCLUDE_DIRS "")
set(DEVICE_DEFINITIONS "")

# --- Airspy --------------------------------------------------
pkg_check_modules(AIRSPY QUIET libairspy)
if(AIRSPY_FOUND)
    message(STATUS "[stream1090] Airspy support enabled")
    list(APPEND DEVICE_SOURCES       src/devices/AirspyDevice.cpp)
    list(APPEND DEVICE_LIBS          ${AIRSPY_LIBRARIES})
    list(APPEND DEVICE_INCLUDE_DIRS  ${AIRSPY_INCLUDE_DIRS})
    list(APPEND DEVICE_DEFINITIONS   STREAM1090_HAVE_AIRSPY)
else()
    message(STATUS "[stream1090] Airspy support disabled (libairspy not found)")
endif()

# --- RTL-SDR -------------------------------------------------
pkg_check_modules(RTLSDR QUIET librtlsdr)
if(RTLSDR_FOUND)
    message(STATUS "[stream1090] RTL-SDR support enabled")
    list(APPEND DEVICE_SOURCES       src/devices/RtlSdrDevice.cpp)
    list(APPEND DEVICE_LIBS          ${RTLSDR_LIBRARIES})
    list(APPEND DEVICE_INCLUDE_DIRS  ${RTLSDR_INCLUDE_DIRS})
    list(APPEND DEVICE_DEFINITIONS   STREAM1090_HAVE_RTLSDR)
else()
    message(STATUS "[stream1090] RTL-SDR support disabled (librtlsdr not found)")
endif()

if (ENABLE_CUSTOM_INPUT)
    message(STATUS "[stream1090] Custom input mode enabled")
endif()

# ------------------------------------------------------------
# Core sources
# ------------------------------------------------------------
file(GLOB STREAM1090_CORE src/*.cpp)

# ------------------------------------------------------------
# Main executable (formerly stream1090_test)
# ------------------------------------------------------------
add_executable(stream1090
    main.cpp
    ${STREAM1090_CORE}
    ${DEVICE_SOURCES}
)

target_include_directories(stream1090 PRIVATE
    include
    ${DEVICE_INCLUDE_DIRS}
)

target_compile_options(stream1090 PRIVATE ${DEFAULT_COMPILE_OPTIONS})

target_compile_definitions(stream1090 PRIVATE
    ${STATS_DEF}
    ${STATS_END_DEF}
    ${OUTPUT_DEF}
    ${CUSTOM_INPUT_DEF}
    ${DEVICE_DEFINITIONS}
)

target_link_libraries(stream1090 PRIVATE ${DEVICE_LIBS})

# ------------------------------------------------------------
# table_gen (excluded from all)
# ------------------------------------------------------------
add_executable(table_gen table_gen.cpp)
target_include_directories(table_gen PRIVATE include)
target_compile_options(table_gen PRIVATE ${DEFAULT_COMPILE_OPTIONS})
set_target_properties(table_gen PROPERTIES EXCLUDE_FROM_ALL TRUE)
