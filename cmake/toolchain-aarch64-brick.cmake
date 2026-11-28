# Cross-compilation settings for the TrimUI Brick Pro (Allwinner A133P, aarch64).
#
# Official TrimUI Linaro 7.5.0 ships a GCC 7 without <filesystem>, so this
# project needs the Arm GNU 8.3 toolchain MinUI uses. Merge the TrimUI SDK user
# tree into that toolchain first:
#
#   rsync -a --ignore-existing SDK_usr/ <tc>/aarch64-linux-gnu/libc/usr/
#
# Note --ignore-existing: the other direction lets the SDK 2.23 libm/libpthread
# overwrite the toolchain copies and breaks the link.
#
# Usage:
#   cmake -S . -B build-brick -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-brick.cmake \
#     -DMRG_CROSS_PREFIX=<tc>/bin/aarch64-linux-gnu- \
#     -DMRG_SYSROOT=<tc>/aarch64-linux-gnu/libc \
#     -DMRG_PLATFORM=trimui
#
# CMake re-enters this file inside try_compile with the compilers already
# resolved and user cache entries not forwarded, so every setting below is
# applied only when it is still empty and the sysroot is derived from the
# compiler location rather than required from the command line.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# try_compile starts a nested configure that only receives the platform variables
# named here; without this the sysroot would look unset while probing the
# compiler and the probe would silently use host headers.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MRG_SYSROOT MRG_CROSS_PREFIX)

if(NOT MRG_CROSS_PREFIX)
  set(MRG_CROSS_PREFIX aarch64-linux-gnu- CACHE STRING "Cross tool prefix")
endif()
if(NOT CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER "${MRG_CROSS_PREFIX}gcc")
endif()
if(NOT CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER "${MRG_CROSS_PREFIX}g++")
endif()
if(NOT CMAKE_AR)
  set(CMAKE_AR "${MRG_CROSS_PREFIX}ar")
endif()
if(NOT CMAKE_RANLIB)
  set(CMAKE_RANLIB "${MRG_CROSS_PREFIX}ranlib")
endif()
if(NOT CMAKE_STRIP)
  set(CMAKE_STRIP "${MRG_CROSS_PREFIX}strip")
endif()
if(NOT CMAKE_READELF)
  set(CMAKE_READELF "${MRG_CROSS_PREFIX}readelf")
endif()
if(NOT CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "${MRG_CROSS_PREFIX}objdump")
endif()

# The sysroot is the toolchain libc directory with the SDK user tree merged in,
# so device headers and shared libraries resolve from one place. Defaulting it
# from the compiler keeps try_compile working without extra flags.
if(NOT MRG_SYSROOT)
  if(IS_ABSOLUTE "${CMAKE_CXX_COMPILER}")
    get_filename_component(_mrg_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_mrg_tc_prefix "${_mrg_bin_dir}" DIRECTORY)
    set(MRG_SYSROOT "${_mrg_tc_prefix}/aarch64-linux-gnu/libc" CACHE PATH
      "TrimUI SDK sysroot (toolchain libc with usr/ merged)")
  else()
    message(FATAL_ERROR "Pass -DMRG_SYSROOT=<toolchain>/aarch64-linux-gnu/libc")
  endif()
endif()
set(CMAKE_SYSROOT "${MRG_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT} ${CMAKE_SYSROOT}/usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The device runs an old glibc, so optimisation flags stay at the baseline; only
# dead-code elimination is added, by the project targets themselves.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--as-needed -Wl,-O1")

# The sysroot carries no usable .pc files, and the SDL2 there has no CMake
# package config, so pkg-config would only add noise: CMakeLists.txt finds
# SDL2 with find_path/find_library instead.
set(PKG_CONFIG_EXECUTABLE "" CACHE FILEPATH "" FORCE)
