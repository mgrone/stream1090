cmake_minimum_required(VERSION 3.10)
project(stream1090 VERSION 1.0.0)

# Must come before the first install(): CMAKE_INSTALL_BINDIR etc. are defined here.
include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

# ------------------------------------------------------------
# Options
# ------------------------------------------------------------
option(ENABLE_STATS          "Compile with STATS_ENABLED" ON)
option(END_STATS             "Compile with STATS_END_ONLY" OFF)
option(ENABLE_RSSI           "Compile with STREAM1090_ENABLE_RSSI" ON)

option(ENABLE_PREAMBLE_GATE  "Reject address-parity frames that are weak and preamble-less" ON)
set(PREAMBLE_GATE_THRESHOLD 1.2 CACHE STRING "Preamble score a real transmission must reach")
set(MIN_SNR               2.5 CACHE STRING "Reject address-parity frames below this signal-to-noise ratio (0 disables; a ratio, so it transfers between receivers)")
option(ENABLE_RTLSDR_BLOG    "Enable vendored RTL-SDR Blog fork" ON)
option(BUILD_TESTING         "Build tests" OFF)

# Error that this has been removed 
option(ENABLE_TOO_MUCH_CPU   "Removed. Use stream1090_airspy instead" OFF)
if (ENABLE_TOO_MUCH_CPU)
    message(FATAL_ERROR "ENABLE_TOO_MUCH_CPU has been removed. Use stream1090_airspy instead.")
endif()

set(STATS_DEF        STATS_ENABLED=$<BOOL:${ENABLE_STATS}>)
set(STATS_END_DEF    STATS_END_ONLY=$<BOOL:${END_STATS}>)
set(RSSI_DEF         STREAM1090_RSSI=$<BOOL:${ENABLE_RSSI}>)
set(PREAMBLE_GATE_DEF STREAM1090_PREAMBLE_GATE=$<BOOL:${ENABLE_PREAMBLE_GATE}>)
set(PREAMBLE_THRESHOLD_DEF STREAM1090_PREAMBLE_THRESHOLD=${PREAMBLE_GATE_THRESHOLD})
set(MIN_SNR_DEF         STREAM1090_MIN_SNR=${MIN_SNR})

# ------------------------------------------------------------
# Compiler settings & base flags
# ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DEFAULT_COMPILE_OPTIONS -Wall -Wextra -Wunused-function -O3 -ffast-math)
# e.g. cmake -DSTREAM1090_ARCH_FLAGS="-march=x86-64-v2" (avoid -march=native in containers)
if(STREAM1090_ARCH_FLAGS)
    list(APPEND DEFAULT_COMPILE_OPTIONS ${STREAM1090_ARCH_FLAGS})
endif()

# Params template: no library is built, targets just inherit these.
add_library(stream1090_common_params INTERFACE)
target_include_directories(stream1090_common_params INTERFACE include presets)
target_compile_options(stream1090_common_params INTERFACE ${DEFAULT_COMPILE_OPTIONS})
target_compile_definitions(stream1090_common_params INTERFACE
    ${STATS_DEF} ${STATS_END_DEF} ${RSSI_DEF}
    ${PREAMBLE_GATE_DEF} ${PREAMBLE_THRESHOLD_DEF} ${MIN_SNR_DEF}
)

file(GLOB STREAM1090_CORE src/*.cpp)

# ==============================================================================
# Backends: find or build each device library ONCE.
# pkg_check_modules(<PREFIX> ...) creates <PREFIX>_FOUND, <PREFIX>_LDFLAGS and
# <PREFIX>_INCLUDE_DIRS - always use exactly the same prefix when reading them.
# ==============================================================================

# --- RTL-SDR: system librtlsdr (stream1090_rtlsdr) ---
pkg_check_modules(LIB_RTLSDR QUIET librtlsdr)

# --- RTL-SDR Blog fork: vendored in thirdparty/ (stream1090_rtlsdr_blog) ---
# The fork needs libusb and stops the whole configure without it, so look for
# libusb first and fall back to the system librtlsdr (or no RTL-SDR) instead.
if(ENABLE_RTLSDR_BLOG)
    pkg_check_modules(LIB_USB QUIET libusb-1.0)
    if(LIB_USB_FOUND)
        set(DETACH_KERNEL_DRIVER ON CACHE BOOL "" FORCE)
        add_subdirectory(thirdparty/rtl-sdr-blog EXCLUDE_FROM_ALL)
        set(LIB_RTLSDR_BLOG_FOUND ON)
    else()
        message(WARNING "stream1090: libusb-1.0 not found, not building the vendored RTL-SDR Blog fork")
    endif()
endif()

# --- Airspy (system library) ---
pkg_check_modules(LIB_AIRSPY QUIET libairspy)

# --- Gather all RTL-SDR related files
set(RTLSDR_SOURCES "")
set(RTLSDR_LIBS "")
set(RTLSDR_INCLUDES "")
set(RTLSDR_DEFS "")
if(LIB_RTLSDR_BLOG_FOUND)
    set(RTLSDR_SOURCES  src/devices/RtlSdrDevice.cpp)
    set(RTLSDR_LIBS     rtlsdr_static)
    set(RTLSDR_INCLUDES thirdparty/rtl-sdr-blog/include)
    set(RTLSDR_DEFS     STREAM1090_HAS_LIB_RTLSDR_BLOG)
    message(STATUS "[stream1090] RTL-SDR: vendored RTL-SDR Blog fork")
elseif(LIB_RTLSDR_FOUND)
    set(RTLSDR_SOURCES  src/devices/RtlSdrDevice.cpp)
    set(RTLSDR_LIBS     ${LIB_RTLSDR_LDFLAGS})
    set(RTLSDR_INCLUDES ${LIB_RTLSDR_INCLUDE_DIRS})
    set(RTLSDR_DEFS     STREAM1090_HAS_LIB_RTLSDR)
    message(STATUS "[stream1090] RTL-SDR: system librtlsdr")
else()
    message(STATUS "[stream1090] RTL-SDR: disabled (neither the Blog fork nor a system librtlsdr)")
endif()

# --- Gather all Airspy related files
set(AIRSPY_SOURCES "")
set(AIRSPY_LIBS "")
set(AIRSPY_INCLUDES "")
set(AIRSPY_DEFS "")
if(LIB_AIRSPY_FOUND)
    set(AIRSPY_SOURCES  src/devices/AirspyDevice.cpp)
    set(AIRSPY_LIBS     ${LIB_AIRSPY_LDFLAGS})
    set(AIRSPY_INCLUDES ${LIB_AIRSPY_INCLUDE_DIRS})
    set(AIRSPY_DEFS     STREAM1090_HAS_LIB_AIRSPY)
endif()

# --- For AD936x and Custom there is nothing to do

# ==============================================================================
# Executable variants
#   stream1090_add_variant(<target> [DEFAULT]
#       [SOURCES ...] [LIBS ...] [INCLUDES ...] [DEFS ...])
# DEFAULT  -> part of "all" and installed normally
# otherwise -> EXCLUDE_FROM_ALL, installed only via `--component variants`
#              (installing an EXCLUDE_FROM_ALL target in the normal install
#              set makes `make install` fail unless it was built first)
# ==============================================================================
function(stream1090_add_variant target)
    cmake_parse_arguments(V "DEFAULT" "" "SOURCES;LIBS;INCLUDES;DEFS" ${ARGN})

    add_executable(${target} main.cpp ${STREAM1090_CORE} ${V_SOURCES})
    target_link_libraries(${target} PRIVATE stream1090_common_params ${V_LIBS})
    if(V_INCLUDES)
        target_include_directories(${target} PRIVATE ${V_INCLUDES})
    endif()
    if(V_DEFS)
        target_compile_definitions(${target} PRIVATE ${V_DEFS})
    endif()

    if(V_DEFAULT)
        install(TARGETS ${target} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL ON)
        install(TARGETS ${target}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT variants EXCLUDE_FROM_ALL)
    endif()
endfunction()

# --- Per-device variants (each one always exists; without its library it is built
#     without device support, i.e. without the matching STREAM1090_HAS_LIB_* define) ---
stream1090_add_variant(stream1090_rtlsdr
    SOURCES ${RTLSDR_SOURCES} 
    LIBS ${RTLSDR_LIBS}
    INCLUDES ${RTLSDR_INCLUDES}
    DEFS ${RTLSDR_DEFS} STREAM1090_PRESETS_FILE="RtlSdrPresets.hpp")

stream1090_add_variant(stream1090_airspy
    SOURCES ${AIRSPY_SOURCES} 
    LIBS ${AIRSPY_LIBS}
    INCLUDES ${AIRSPY_INCLUDES}
    DEFS ${AIRSPY_DEFS} STREAM1090_PRESETS_FILE="AirspyPresets.hpp")

stream1090_add_variant(stream1090_ad936x
    DEFS STREAM1090_PRESETS_FILE="Ad936xPresets.hpp")

stream1090_add_variant(stream1090_custom
    DEFS STREAM1090_PRESETS_FILE="CustomPresets.hpp")

# --- Default target ("make" / "make stream1090"): every backend that is available ---
set(DEFAULT_SOURCES "")
set(DEFAULT_LIBS "")
set(DEFAULT_INCLUDES "")
set(DEFAULT_DEFS "")
list(APPEND DEFAULT_SOURCES  ${RTLSDR_SOURCES}  ${AIRSPY_SOURCES})
list(APPEND DEFAULT_LIBS     ${RTLSDR_LIBS}     ${AIRSPY_LIBS})
list(APPEND DEFAULT_INCLUDES ${RTLSDR_INCLUDES} ${AIRSPY_INCLUDES})
list(APPEND DEFAULT_DEFS     ${RTLSDR_DEFS}     ${AIRSPY_DEFS})

if(NOT DEFAULT_SOURCES)
    message(WARNING "stream1090: no SDR backend found, the default target has no device input")
endif()

stream1090_add_variant(stream1090 DEFAULT
    SOURCES ${DEFAULT_SOURCES} LIBS ${DEFAULT_LIBS}
    INCLUDES ${DEFAULT_INCLUDES}
    DEFS ${DEFAULT_DEFS} STREAM1090_PRESETS_FILE="DefaultPresets.hpp")

# ------------------------------------------------------------
# Tests
# ------------------------------------------------------------
include(CTest)

function(stream1090_add_test target source)
    add_executable(${target} ${source})
    # presets/ + STREAM1090_PRESETS_FILE: same as the product, in case headers in
    # include/ pull in the presets file.
    target_include_directories(${target} PRIVATE include presets)
    target_compile_definitions(${target} PRIVATE STREAM1090_PRESETS_FILE="DefaultPresets.hpp")
    target_compile_options(${target} PRIVATE ${DEFAULT_COMPILE_OPTIONS})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

if(BUILD_TESTING)
    stream1090_add_test(crc_error_table_test tests/CRCErrorTableTest.cpp)
    stream1090_add_test(icao_cache_test tests/ICAOCacheTest.cpp)
    stream1090_add_test(df11_interrogator_test tests/DF11InterrogatorTest.cpp)
    stream1090_add_test(ring_buffer_test tests/RingBufferTest.cpp)
    stream1090_add_test(erasure_repair_test tests/ErasureRepairTest.cpp)
    stream1090_add_test(low_pass_filter_test tests/LowPassFilterTest.cpp)
    stream1090_add_test(sampler_test tests/SamplerTest.cpp)
    stream1090_add_test(mode_s_altitude_test tests/ModeSAltitudeTest.cpp)
    stream1090_add_test(mode_s_position_test tests/ModeSPositionTest.cpp)
    stream1090_add_test(rtlsdr_serial_test tests/RtlSdrSerialTest.cpp)
    stream1090_add_test(sample_drop_event_gate_test tests/SampleDropEventGateTest.cpp)
    stream1090_add_test(noise_false_positive_test tests/NoiseFalsePositiveTest.cpp)
    stream1090_add_test(trust_gate_test tests/TrustGateTest.cpp)
    stream1090_add_test(trust_separation_test tests/TrustSeparationTest.cpp)
    # Regressions covering DF18/DF19/DF11-PI plausibility fixes. See README_ADV.md.
    stream1090_add_test(df18_fine_tisb_regression_test tests/Df18FineTisbRegressionTest.cpp)
    stream1090_add_test(df19_repair_trust_regression_test tests/Df19RepairTrustRegressionTest.cpp)
    stream1090_add_test(df11_pi_icao_bypass_regression_test tests/Df11PiIcaoBypassRegressionTest.cpp)
    target_compile_definitions(noise_false_positive_test PRIVATE
        ${PREAMBLE_GATE_DEF}
        ${PREAMBLE_THRESHOLD_DEF}
        ${MIN_SNR_DEF}
    )
endif()

# ------------------------------------------------------------
# Install rules (binaries are installed by stream1090_add_variant)
# ------------------------------------------------------------
install(FILES configs/rtlsdr.ini configs/airspy.ini
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/stream1090
)

install(DIRECTORY custom_filters/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/stream1090/custom_filters
)

install(DIRECTORY filter_utils/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/stream1090/filter_utils
)

# ------------------------------------------------------------
# CPack / DEB packaging
# ------------------------------------------------------------
set(CPACK_PACKAGE_NAME "stream1090")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Joseph Prochazka <joe@swiftbyte.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Mode-S demodulator for ADS-B reception via SDR")
# One string: set() with several arguments would build a ;-separated list.
set(CPACK_PACKAGE_DESCRIPTION
"stream1090 reads IQ samples from an Airspy or RTL-SDR dongle and outputs
raw Mode-S message frames to stdout using CRC-based message framing.
It is designed to feed a decoder such as readsb or dump1090-fa via socat.")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "socat")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/jprochazka/stream1090")

include(CPack)

# ------------------------------------------------------------
# table_gen (excluded from all)
# ------------------------------------------------------------
add_executable(table_gen table_gen.cpp)
target_include_directories(table_gen PRIVATE include)
target_compile_options(table_gen PRIVATE ${DEFAULT_COMPILE_OPTIONS})
set_target_properties(table_gen PROPERTIES EXCLUDE_FROM_ALL TRUE)
