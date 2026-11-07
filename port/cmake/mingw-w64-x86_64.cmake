# CMake toolchain file: the Windows build (x86-64), cross-compiled on Linux
# with MinGW-w64 GCC.
#
#   tools/build_windows.sh                      # all of it: SDL3, build, dist folder
#
# or by hand:
#   tools/build_sdl3_mingw.sh                   # once: SDL3 into tools/toolchain/mingw64
#   python tools/pc_sources.py
#   cmake -S port -B build/pc/win -G Ninja --toolchain port/cmake/mingw-w64-x86_64.cmake
#   cmake --build build/pc/win
#
# The compiler, ld and objcopy are the installed x86_64-w64-mingw32-* tools.
# SDL3 is the one tools/build_sdl3_mingw.sh builds. Vulkan is headers only, as
# on Linux: the loader (vulkan-1.dll) is found at run time through SDL.
# The game's own layout probes (tools/pc_link.py) run with the host gcc: the
# game types lay out the same on both x86-64 targets, given -mno-ms-bitfields
# (port/CMakeLists.txt) and no `long` in game code (32 bits on Windows).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(XG_MINGW_TRIPLE x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${XG_MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${XG_MINGW_TRIPLE}-g++) # SDL3 has some C++
set(CMAKE_RC_COMPILER ${XG_MINGW_TRIPLE}-windres)

get_filename_component(XG_MINGW_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../tools/toolchain/mingw64" ABSOLUTE)

# Where the target's libraries and packages are: the compiler's own sysroot
# and the local SDL3 prefix. Programs (glslc, python) come from the host.
execute_process(COMMAND ${CMAKE_C_COMPILER} -print-sysroot
                OUTPUT_VARIABLE XG_MINGW_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(CMAKE_FIND_ROOT_PATH "${XG_MINGW_PREFIX}")
if(XG_MINGW_SYSROOT)
    list(APPEND CMAKE_FIND_ROOT_PATH "${XG_MINGW_SYSROOT}/${XG_MINGW_TRIPLE}" "${XG_MINGW_SYSROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(SDL3_DIR "${XG_MINGW_PREFIX}/lib/cmake/SDL3" CACHE PATH "SDL3 for MinGW (tools/build_sdl3_mingw.sh)")

# Vulkan: the system's headers, through a directory that holds only them
# (vulkan/ and vk_video/ link to /usr/include), so the host's C headers stay
# out of the target's include path. FindVulkan insists on a library, which
# nothing links: the loader is loaded at run time.
set(Vulkan_INCLUDE_DIR "${XG_MINGW_PREFIX}/vulkan/include" CACHE PATH "Vulkan headers only")
set(Vulkan_LIBRARY "vulkan-1" CACHE FILEPATH "not linked: SDL loads vulkan-1.dll at run time")

# The per-module link (port/CMakeLists.txt): the target's ld and objcopy.
find_program(XG_LD NAMES ${XG_MINGW_TRIPLE}-ld REQUIRED)
find_program(XG_OBJCOPY NAMES ${XG_MINGW_TRIPLE}-objcopy REQUIRED)
# tools/pc_link.py's layout probes: the host compiler (see above).
find_program(XG_PROBE_CC NAMES gcc cc REQUIRED)
