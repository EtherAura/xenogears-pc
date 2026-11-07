# CMake toolchain file: an aarch64 (arm64) Linux build, cross-compiled on an
# x86-64 Linux host. A portability check on the way to Android (xg-mrj.3):
# the same CPU and ABI as an arm64 phone, with glibc and the host's habits.
#
#   python tools/pc_sources.py
#   cmake -S port -B build/pc/arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         --toolchain $PWD/port/cmake/aarch64-linux-gnu.cmake
#   cmake --build build/pc/arm64
#
# XG_AARCH64_TRIPLE names the cross tools (default aarch64-unknown-linux-gnu:
# <triple>-gcc, -g++, -ld, -objcopy) and XG_AARCH64_SYSROOT the target's root
# (default: <triple>-gcc -print-sysroot), which must hold SDL3 and the
# Vulkan headers. Programs (glslc, python) come from the host. The game's
# layout probes (tools/pc_link.py) run with the host gcc: x86-64 and aarch64
# are both LP64 and lay the game's types out alike.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT XG_AARCH64_TRIPLE)
    set(XG_AARCH64_TRIPLE aarch64-unknown-linux-gnu)
endif()
# GCC unless the compilers are given (-DCMAKE_C_COMPILER=<triple>-clang
# -DCMAKE_CXX_COMPILER=<triple>-clang++: the NDK's compiler family).
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER ${XG_AARCH64_TRIPLE}-gcc)
endif()
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER ${XG_AARCH64_TRIPLE}-g++)
endif()

if(NOT XG_AARCH64_SYSROOT)
    # (GCC's: clang has no -print-sysroot)
    execute_process(COMMAND ${XG_AARCH64_TRIPLE}-gcc -print-sysroot
                    OUTPUT_VARIABLE XG_AARCH64_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
set(CMAKE_SYSROOT "${XG_AARCH64_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${XG_AARCH64_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Vulkan: the target's headers; the loader is found at run time through SDL,
# so the library is only named. glslc is the host's.
find_program(Vulkan_GLSLC_EXECUTABLE NAMES glslc REQUIRED)

# The per-module link (port/CMakeLists.txt): the target's ld and objcopy.
find_program(XG_LD NAMES ${XG_AARCH64_TRIPLE}-ld REQUIRED)
find_program(XG_OBJCOPY NAMES ${XG_AARCH64_TRIPLE}-objcopy REQUIRED)
# tools/pc_link.py's layout probes: the host compiler (see above).
find_program(XG_PROBE_CC NAMES gcc cc REQUIRED)
