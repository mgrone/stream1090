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
option(ENABLE_STATS          "Compile with STATS_ENABLED" ON)
option(END_STATS             "Compile with STATS_END_ONLY" OFF)
option(ENABLE_CUSTOM_INPUT   "Compile with STREAM1090_CUSTOM_INPUT" OFF)
option(ENABLE_RSSI           "Compile with STREAM1090_ENABLE_RSSI" ON)
option(ENABLE_RTLSDR_BLOG    "Enable vendored RTL-SDR Blog fork" OFF)
option(ENABLE_TOO_MUCH_CPU   "Unlocks the 40 and 48 Msps speeds" OFF)
option(BUILD_TESTING         "Build tests" OFF)

set(STATS_DEF        STATS_ENABLED=$<BOOL:${ENABLE_STATS}>)
set(STATS_END_DEF    STATS_END_ONLY=$<BOOL:${END_STATS}>)
set(CUSTOM_INPUT_DEF STREAM1090_CUSTOM_INPUT=$<BOOL:${ENABLE_CUSTOM_INPUT}>)
set(RSSI_DEF         STREAM1090_RSSI=$<BOOL:${ENABLE_RSSI}>)
set(TOO_MUCH_CPU_DEF STREAM1090_TOO_MUCH_CPU=$<BOOL:${ENABLE_TOO_MUCH_CPU}>)

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
    list(APPEND DEVICE_LIBS          ${AIRSPY_LDFLAGS})
    list(APPEND DEVICE_INCLUDE_DIRS  ${AIRSPY_INCLUDE_DIRS})
    list(APPEND DEVICE_DEFINITIONS   STREAM1090_HAVE_AIRSPY)
else()
    message(STATUS "[stream1090] Airspy support disabled (libairspy not found)")
endif()

# ------------------------------------------------------------
# RTL-SDR backend
# ------------------------------------------------------------
if(ENABLE_RTLSDR_BLOG)
    message(STATUS "[stream1090] Using vendored RTL-SDR Blog fork")

    set(DETACH_KERNEL_DRIVER ON CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/rtl-sdr-blog)

    list(APPEND DEVICE_SOURCES       src/devices/RtlSdrDevice.cpp)
    list(APPEND DEVICE_LIBS          rtlsdr)
    list(APPEND DEVICE_INCLUDE_DIRS  thirdparty/rtl-sdr-blog/include)
    list(APPEND DEVICE_DEFINITIONS   STREAM1090_HAVE_RTLSDR)
    list(APPEND DEVICE_DEFINITIONS   STREAM1090_HAVE_RTLSDR_BLOG)

else()
    message(STATUS "[stream1090] Trying system librtlsdr...")

    find_package(PkgConfig)
    pkg_check_modules(RTLSDR QUIET librtlsdr)

    if(RTLSDR_FOUND)
        message(STATUS "[stream1090] System librtlsdr found")

        list(APPEND DEVICE_SOURCES       src/devices/RtlSdrDevice.cpp)
        list(APPEND DEVICE_LIBS          ${RTLSDR_LDFLAGS})
        list(APPEND DEVICE_INCLUDE_DIRS  ${RTLSDR_INCLUDE_DIRS})
        list(APPEND DEVICE_DEFINITIONS   STREAM1090_HAVE_RTLSDR)

    else()
        message(STATUS "[stream1090] RTL-SDR support disabled (no Blog fork, no system librtlsdr)")
    endif()
endif()


# ------------------------------------------------------------
# Feature flags
# ------------------------------------------------------------
if (ENABLE_RSSI)
    message(STATUS "[stream1090] RSSI output enabled")
else()
    message(STATUS "[stream1090] RSSI output disabled")
endif()

if (ENABLE_CUSTOM_INPUT)
    message(STATUS "[stream1090] Custom input mode enabled")
endif()

# ------------------------------------------------------------
# Core sources
# ------------------------------------------------------------
file(GLOB STREAM1090_CORE src/*.cpp)

# ------------------------------------------------------------
# Main executable
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
    ${CUSTOM_INPUT_DEF}
    ${RSSI_DEF}
    ${TOO_MUCH_CPU_DEF}
    ${DEVICE_DEFINITIONS}
)

target_link_libraries(stream1090 PRIVATE ${DEVICE_LIBS})

include(CTest)
if(BUILD_TESTING)
    add_executable(crc_error_table_test tests/CRCErrorTableTest.cpp)
    target_include_directories(crc_error_table_test PRIVATE include)
    target_compile_options(crc_error_table_test PRIVATE ${DEFAULT_COMPILE_OPTIONS})
    add_test(NAME crc_error_table_test COMMAND crc_error_table_test)

    add_executable(icao_cache_test tests/ICAOCacheTest.cpp)
    target_include_directories(icao_cache_test PRIVATE include)
    target_compile_options(icao_cache_test PRIVATE ${DEFAULT_COMPILE_OPTIONS})
    add_test(NAME icao_cache_test COMMAND icao_cache_test)

    add_executable(df11_interrogator_test tests/DF11InterrogatorTest.cpp)
    target_include_directories(df11_interrogator_test PRIVATE include)
    target_compile_options(df11_interrogator_test PRIVATE ${DEFAULT_COMPILE_OPTIONS})
    add_test(NAME df11_interrogator_test COMMAND df11_interrogator_test)

    add_executable(ring_buffer_test tests/RingBufferTest.cpp)
    target_include_directories(ring_buffer_test PRIVATE include)
    target_compile_options(ring_buffer_test PRIVATE ${DEFAULT_COMPILE_OPTIONS})
    add_test(NAME ring_buffer_test COMMAND ring_buffer_test)

    add_executable(erasure_repair_test tests/ErasureRepairTest.cpp)
    target_include_directories(erasure_repair_test PRIVATE include)
    target_compile_options(erasure_repair_test PRIVATE ${DEFAULT_COMPILE_OPTIONS})
    add_test(NAME erasure_repair_test COMMAND erasure_repair_test)

    add_executable(sampler_test tests/SamplerTest.cpp)
    target_include_directories(sampler_test PRIVATE include)
    target_compile_options(sampler_test PRIVATE ${DEFAULT_COMPILE_OPTIONS})
    add_test(NAME sampler_test COMMAND sampler_test)
endif()

# ------------------------------------------------------------
# table_gen (excluded from all)
# ------------------------------------------------------------
add_executable(table_gen table_gen.cpp)
target_include_directories(table_gen PRIVATE include)
target_compile_options(table_gen PRIVATE ${DEFAULT_COMPILE_OPTIONS})
set_target_properties(table_gen PROPERTIES EXCLUDE_FROM_ALL TRUE)
