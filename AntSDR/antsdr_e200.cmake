set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Map compilers using the environment variable we exported earlier
set(TOOLCHAIN_BIN "$ENV{TOOLCHAIN_PATH}/bin")
set(CMAKE_C_COMPILER "${TOOLCHAIN_BIN}/arm-none-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_BIN}/arm-none-linux-gnueabihf-g++")

# Optimization flags targeting the AntSDR Zynq-7020 Cortex-A9 core
set(OBJECT_GEN_FLAGS "-mcpu=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=hard")
set(CMAKE_C_FLAGS "${OBJECT_GEN_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${OBJECT_GEN_FLAGS}" CACHE STRING "" FORCE)

# Force static linking so the binary carries its own modern C++ runtime libraries
set(CMAKE_EXE_LINKER_FLAGS "-static" CACHE STRING "" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# ------------------------------------------------------------
# AntSDR Hardware Bypasses
# ------------------------------------------------------------
# Force Custom Input mode on so we don't look for USB hardware
# set(ENABLE_CUSTOM_INPUT ON CACHE BOOL "Forced for AntSDR" FORCE)

# Explicitly disable external USB hardware drivers
set(ENABLE_RTLSDR_BLOG OFF CACHE BOOL "Forced for AntSDR" FORCE)
set(AIRSPY_FOUND FALSE CACHE BOOL "Forced for AntSDR" FORCE)
set(RTLSDR_FOUND FALSE CACHE BOOL "Forced for AntSDR" FORCE)

# Prevent PkgConfig from searching your Ubuntu host system
set(ENV{PKG_CONFIG_LIBDIR} "")
set(ENV{PKG_CONFIG_PATH} "")
