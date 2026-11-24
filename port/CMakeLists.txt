# Xenogears PC port (docs/PORT_PLAN.md).
#
#   python tools/pc_sources.py                 # or tools/configure.py: writes build/pc/sources.cmake
#   cmake -S port -B build/pc/cmake -G Ninja
#   cmake --build build/pc/cmake
#
# The game C in src/ is compiled exactly as the matching build has it, with
# PLATFORM_PC defined; the platform layer in port/src stands in for the PsyQ SDK
# and the PS1 hardware. Nothing from the disc is part of the source tree.

cmake_minimum_required(VERSION 3.25)
project(xenogears_pc C)

# The port's own C is GNU C17, required, on every compiler, not the
# compiler's default (GNU C23 from GCC 15, GNU C17 in Clang and the NDK): it
# uses C11 (_Static_assert, _Alignas, _Atomic and <stdatomic.h>), and C17 is
# C11 with its defects fixed. GNU extensions stay on. Set before any target,
# so every C target has it; the game C's own -std=gnu89 (XG_GAME_OPTIONS)
# comes after it on the command line and still wins. #embed (gpu/wide.c) is
# taken before C23 by GCC and Clang alike. C++ (the menu) keeps cxx_std_17.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Floating point rounds as written, on every target and CPU: no FMA
# contraction, also where the baseline has FMA (aarch64) or XG_MARCH enables
# it. The enhanced rasteriser's CPU tests compare its batched evaluation
# with the original order byte for byte (gp0.c hiSeal, test/seamreusetest.c),
# and replayed pictures must equal live ones. (Not for windres: C/C++ only.)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=off>")
endif()
# Plain char is signed in the port's own C and C++ on every target, as on
# x86-64, where it was written; aarch64 (Linux, Android) makes it unsigned.
# The game C keeps the PS1's unsigned char (XG_GAME_OPTIONS' -funsigned-char
# comes after this). xg-mrj.3.2.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64|arm)")
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fsigned-char>")
endif()

set(XG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# XG_DIST (xg-4oq.9): a build to hand out (release packages, the Android APK).
# It names no folder of the machine that built it. The repository's folder is
# not compiled in (XG_ROOT in the platform layer: the default disc image in
# bin/ and the other engine's build folder are a developer's conveniences, and
# without them a start with no disc goes to the setup screen), and the file
# names in the code (__FILE__) and its debug information are relative to the
# repository. tools/payloadcheck.py checks such a build's executable for those
# folders. Off (the default) keeps a development build as it was.
option(XG_DIST "a build to hand out: no path of the build machine in it" OFF)
# A package with a release's version (cmake/deb.cmake: -DXG_PACKAGE_VERSION=<the
# tag>, not the development default) is handed out by definition, whatever the
# build folder was configured as before.
if(DEFINED XG_PACKAGE_VERSION AND NOT XG_PACKAGE_VERSION MATCHES "^(0~dev)?$" AND NOT XG_DIST)
    set(XG_DIST ON CACHE BOOL "a build to hand out: no path of the build machine in it" FORCE)
endif()
if(XG_DIST)
    get_filename_component(XG_ROOT_ABS "${XG_ROOT}" ABSOLUTE)
    get_filename_component(XG_ROOT_REAL "${XG_ROOT}" REALPATH)
    # The builder's home too: the toolchain's own headers often live there (an NDK, an
    # SDK) and their folders reach the debug information. The most general map first:
    # where several match, GCC and Clang 17+ take the last one given and older Clang the
    # longest, so the repository and the build folder win either way. A home shorter than
    # /home/x is left alone, as tools/payloadcheck.py leaves it.
    set(XG_DIST_MAPS "")
    string(LENGTH "$ENV{HOME}" XG_HOME_LENGTH)
    if(XG_HOME_LENGTH GREATER 6)
        list(APPEND XG_DIST_MAPS "-ffile-prefix-map=$ENV{HOME}/=")
    endif()
    if(NOT XG_ROOT_REAL STREQUAL XG_ROOT_ABS)
        list(APPEND XG_DIST_MAPS "-ffile-prefix-map=${XG_ROOT_REAL}/=")
    endif()
    list(APPEND XG_DIST_MAPS "-ffile-prefix-map=${XG_ROOT_ABS}/=" "-ffile-prefix-map=${CMAKE_BINARY_DIR}/=build/")
    # The build folder itself too, with no separator after it: the compiler's working folder,
    # the compilation folder of the debug information (XG_STATE_SCHEMA's -g), which the maps
    # above only reach inside the repository or the home. As spelled and resolved (a build
    # folder reached through a symbolic link: the working folder is the resolved one).
    get_filename_component(XG_BINARY_REAL "${CMAKE_BINARY_DIR}" REALPATH)
    list(APPEND XG_DIST_MAPS "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
    if(NOT XG_BINARY_REAL STREQUAL CMAKE_BINARY_DIR)
        list(APPEND XG_DIST_MAPS "-ffile-prefix-map=${XG_BINARY_REAL}=build")
    endif()
    foreach(map IN LISTS XG_DIST_MAPS)
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${map}>")
        # and at GCC's link: its link-time optimisation compiles again there, and that debug
        # information names the link's folder (the build folder) otherwise
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            add_link_options("${map}")
        endif()
    endforeach()
endif()

# XG_STATE_SCHEMA: the image's state schema (tools/state_schema.py,
# port/src/system/stateportable.h), with which a slot this build saves loads
# in another build of the port and another build's slot loads here. It is read
# from the image's debug information, so every configuration compiles and
# links with -g (GCC and Clang generate the same code with it; what ships is
# stripped: cmake --install --strip, CPack, the Android packaging), and each
# link writes the image's schema beside it: state_schema.json (and .gz, for
# tools/statecanon.py) and <image>.xgschema, packed, with no path of this
# machine, which the install rules ship (XG_STATE_SCHEMA_FILE below). About
# 10 seconds per link of the game, nothing when it is not relinked. Needs
# llvm-dwarfdump (LLVM, or the Android NDK's): without it a development build
# writes no schema (its slots then load in it alone) and a build to hand out
# stops. A clang build (Android) also needs the host's GCC (below). Off on
# macOS (the schema tool reads ELF and PE images).
if(APPLE)
    set(xg_state_schema_default OFF)
else()
    set(xg_state_schema_default ON)
endif()
option(XG_STATE_SCHEMA "compile with debug information and write the state schema beside the image"
    ${xg_state_schema_default})
if(XG_STATE_SCHEMA)
    # llvm-dwarfdump: beside the compiler (the NDK's clang), on PATH, or a distribution's versioned LLVM.
    get_filename_component(xg_compiler_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    file(GLOB xg_llvm_bins LIST_DIRECTORIES true "/usr/lib/llvm-*/bin" "/usr/lib/llvm/*/bin")
    list(SORT xg_llvm_bins COMPARE NATURAL ORDER DESCENDING)
    find_program(XG_DWARFDUMP NAMES llvm-dwarfdump HINTS "${xg_compiler_bin}" PATHS ${xg_llvm_bins}
        NO_CMAKE_FIND_ROOT_PATH DOC "llvm-dwarfdump, which tools/state_schema.py reads the image's DWARF with")
    if(NOT XG_DWARFDUMP)
        if(XG_DIST)
            message(FATAL_ERROR "XG_STATE_SCHEMA: no llvm-dwarfdump. A build to hand out ships its state schema: "
                "install LLVM (llvm-dwarfdump), or configure with -DXG_STATE_SCHEMA=OFF for one whose slots load "
                "in it alone.")
        endif()
        message(WARNING "XG_STATE_SCHEMA: no llvm-dwarfdump (LLVM): this build writes no state schema, so the "
            "slots it saves load in it alone. Install LLVM, or -DXG_STATE_SCHEMA=OFF to silence this.")
        set(XG_STATE_SCHEMA OFF)
    else()
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-g>")
        add_link_options(-g)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Clang writes no debug information for extern declarations, which the generated
            # data's types come from (gen/: u8 arrays): the schema tool reads the game C's with the
            # host's GCC, by the build's compile commands, and finds each type by its name in the
            # image's own debug information (tools/state_schema.py clang_declared;
            # cmake/game-modules.cmake keeps every type the game C defines there). Without GCC
            # the schema says its states cannot convert, and the slots load in this build alone.
            set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
            find_program(XG_SCHEMA_GCC NAMES gcc NO_CMAKE_FIND_ROOT_PATH
                DOC "the host's GCC, with which tools/state_schema.py reads a clang build's declarations")
            if(NOT XG_SCHEMA_GCC)
                if(XG_DIST)
                    message(FATAL_ERROR "XG_STATE_SCHEMA: no gcc. A clang build to hand out reads its game C's "
                        "declarations with the host's GCC for its state schema: install GCC, or configure with "
                        "-DXG_STATE_SCHEMA=OFF for one whose slots load in it alone.")
                endif()
                message(WARNING "XG_STATE_SCHEMA: no gcc: this clang build's state schema cannot read the game "
                    "C's declarations, so the slots it saves load in it alone. Install GCC.")
            endif()
        endif()
    endif()
endif()

# Which game C the executable runs (docs/ENGINE_PLAN.md). "original" compiles
# src/, the matched C, as xenogears: the bit-validated game. "enhanced"
# compiles engine/, the Enhanced engine's fork of it (tools/fork_engine.py),
# as xenogears-enhanced. Everything below reaches the game C and its headers
# through XG_GAME_ROOT; the platform layer, the UI and the tools are shared.
set(XG_ENGINE "original" CACHE STRING "Game engine: original (src/) or enhanced (engine/)")
set_property(CACHE XG_ENGINE PROPERTY STRINGS original enhanced)
if(XG_ENGINE STREQUAL "enhanced")
    set(XG_GAME_ROOT "${XG_ROOT}/engine")
    if(NOT EXISTS "${XG_GAME_ROOT}/src")
        message(FATAL_ERROR "${XG_GAME_ROOT}/src missing: run python tools/fork_engine.py")
    endif()
elseif(XG_ENGINE STREQUAL "original")
    set(XG_GAME_ROOT "${XG_ROOT}")
else()
    message(FATAL_ERROR "XG_ENGINE must be original or enhanced, not ${XG_ENGINE}")
endif()
if(XG_ENGINE STREQUAL "enhanced")
    set(XG_ENGINE_ENHANCED 1)
    set(XG_BUILD_TYPE_SUFFIX " enhanced")
else()
    set(XG_ENGINE_ENHANCED 0)
    set(XG_BUILD_TYPE_SUFFIX "")
endif()
set(XG_VERSION "US" CACHE STRING "Game version: US or JP")
set_property(CACHE XG_VERSION PROPERTY STRINGS US JP)

# XG_PIE (xg-mrj.3.3): every object position-independent (-fPIC, without
# semantic interposition: nothing replaces the program's own functions) and
# the executable linked as a PIE, the layout an Android app's shared library
# has; off (the default) keeps the fixed non-PIE executable. A PIE start
# re-executes itself once without address randomisation (main.c; XG_ASLR=1
# keeps it), so its addresses, like a non-PIE build's, are the same at every
# run and save states load across runs.
option(XG_PIE "position-independent code, a PIE executable (the Android layout)" OFF)
# XG_SHARED: the game as a shared library (libxenogears.so, -Bsymbolic, only
# its entry exported) and a small launcher executable that calls it
# (port/src/launcher), the shape of an Android app's native code; implies
# XG_PIE. Linux only.
option(XG_SHARED "the game as libxenogears.so plus a launcher (the Android app's shape)" OFF)
if(XG_SHARED AND WIN32)
    message(FATAL_ERROR "XG_SHARED: Linux (ELF) only")
endif()
if(XG_SHARED OR APPLE)
    set(XG_PIE ON)
endif()
if(XG_PIE)
    set(XG_PIC_OPTIONS -fPIC -fno-semantic-interposition)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
else()
    set(XG_PIC_OPTIONS -fno-pie)
endif()

set(XG_SOURCES_CMAKE "${XG_ROOT}/build/pc/sources.cmake")
if(NOT EXISTS "${XG_SOURCES_CMAKE}")
    message(FATAL_ERROR "${XG_SOURCES_CMAKE} missing: run python tools/pc_sources.py")
endif()

include("${XG_SOURCES_CMAKE}")
set(XG_MODULES ${XG_MODULES_${XG_VERSION}})
foreach(module IN LISTS XG_MODULES)
    set(XG_SOURCES_${module} ${XG_SOURCES_${XG_VERSION}_${module}})
endforeach()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/game-modules.cmake")
option(XG_DARWIN_MODULE_CHECK "Link the US Original field and battle modules without the presentation layer" OFF)
if(XG_DARWIN_MODULE_CHECK)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/darwin-module-check.cmake")
    return()
endif()

find_package(SDL3 REQUIRED CONFIG)

# Synthetic raw images: identity, file-table bounds, pairing and rejected swaps.
add_executable(disc_verifytest EXCLUDE_FROM_ALL src/cd/test/verifytest.c src/cd/verify.c
    src/cd/disc.c src/cd/swap.c src/system/settings.c)
target_include_directories(disc_verifytest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_link_libraries(disc_verifytest PRIVATE SDL3::SDL3)

# The setup screen's discovery (images and BIOS in folders) on synthetic images.
add_executable(disc_setuptest EXCLUDE_FROM_ALL src/cd/test/setuptest.c src/cd/setup.c src/cd/verify.c
    src/cd/disc.c src/system/settings.c)
target_include_directories(disc_setuptest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_link_libraries(disc_setuptest PRIVATE SDL3::SDL3)

# Regional voice bundles and XA routing, using the player's original discs.
add_executable(regional_audiotest EXCLUDE_FROM_ALL src/cd/test/regionaltest.c
    src/cd/regional.c src/cd/disc.c src/cd/verify.c src/cd/xa.c src/system/settings.c)
target_include_directories(regional_audiotest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(regional_audiotest PRIVATE PLATFORM_PC VERSION_US)
target_link_libraries(regional_audiotest PRIVATE SDL3::SDL3)

# The CD drive's timing model on a synthetic disc and a stepped clock.
add_executable(cd_drivetest EXCLUDE_FROM_ALL src/cd/test/drivetest.c src/cd/drive.c src/cd/disc.c
    src/system/settings.c src/system/speedup.c)
target_include_directories(cd_drivetest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(cd_drivetest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_link_libraries(cd_drivetest PRIVATE m SDL3::SDL3)

# Disposable files and SDL virtual devices; no game data or GPU needed.
add_executable(card_historytest EXCLUDE_FROM_ALL src/card/test/historytest.c
    src/card/image.c src/system/settings.c)
target_include_directories(card_historytest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(card_historytest PRIVATE SDL3::SDL3)
add_executable(card_biostest EXCLUDE_FROM_ALL src/card/test/biostest.c
    src/card/bu.c src/card/fs.c src/card/image.c src/system/settings.c src/system/speedup.c)
target_include_directories(card_biostest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(card_biostest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_link_libraries(card_biostest PRIVATE SDL3::SDL3)
add_executable(card_speeduptest EXCLUDE_FROM_ALL src/card/test/speeduptest.c
    src/card/bu.c src/card/fs.c src/card/image.c src/system/settings.c src/system/speedup.c)
target_include_directories(card_speeduptest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(card_speeduptest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_link_libraries(card_speeduptest PRIVATE SDL3::SDL3)
add_executable(pad_profiletest EXCLUDE_FROM_ALL src/system/test/padtest.c
    src/system/pad.c src/system/mouse.c src/system/touch.c src/system/rumble.c src/system/settings.c)
target_include_directories(pad_profiletest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(pad_profiletest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_link_libraries(pad_profiletest PRIVATE SDL3::SDL3)
add_executable(touchtest EXCLUDE_FROM_ALL src/system/test/touchtest.c src/system/touch.c src/system/settings.c)
target_include_directories(touchtest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(touchtest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_link_libraries(touchtest PRIVATE SDL3::SDL3 m)
add_executable(rumbletest EXCLUDE_FROM_ALL src/system/test/rumbletest.c src/system/rumble.c src/system/settings.c)
target_include_directories(rumbletest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(rumbletest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_link_libraries(rumbletest PRIVATE SDL3::SDL3)
add_executable(spu_outputtest EXCLUDE_FROM_ALL src/spu/test/outputtest.c src/system/settings.c)
target_include_directories(spu_outputtest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(spu_outputtest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_link_libraries(spu_outputtest PRIVATE SDL3::SDL3)
# Netplay's protocol and lockstep over a loopback pair (system/netplay_core.c).
add_executable(netplaytest EXCLUDE_FROM_ALL src/system/test/netplaytest.c src/system/netplay_core.c src/system/net.c
    src/system/settings.c)
target_include_directories(netplaytest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(netplaytest PRIVATE SDL3::SDL3)
if(WIN32)
    target_link_libraries(netplaytest PRIVATE ws2_32)
endif()
# MDEC decoder: measured vectors; given the four disc images, retail movie frames.
add_executable(mdectest EXCLUDE_FROM_ALL src/mdec/test/mdectest.c src/mdec/mdec.c src/mdec/vlc.c)
target_include_directories(mdectest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(mdectest PRIVATE PLATFORM_PC)
# Save states (port/src/system/savestate.h): the codec on memory-like data and damaged input.
add_executable(state_codectest EXCLUDE_FROM_ALL src/system/test/statecodectest.c src/system/statecodec.c)
add_executable(stateschematest EXCLUDE_FROM_ALL src/system/test/stateschematest.c src/system/stateschema.c
    src/system/json.c src/system/statecodec.c)
# statecanon.py convert in C (system/stateconvert.c): two small layouts of one program; with a state pair,
# the conversion tools/test_stateconvert_c.py compares with statecanon's.
add_executable(stateconverttest EXCLUDE_FROM_ALL src/system/test/stateconverttest.c src/system/stateconvert.c
    src/system/stateschema.c src/system/json.c src/system/statecodec.c)
add_executable(rewindpacktest EXCLUDE_FROM_ALL src/system/test/rewindpacktest.c src/system/statecodec.c)
# The state file's header (the engine id, region, disc, app version), thumbnail and body round trip.
add_executable(statefiletest EXCLUDE_FROM_ALL src/system/test/statefiletest.c src/system/statefile.c
    src/system/statecodec.c src/system/settings.c src/system/release.c)
# Which release a copy is (release.c, xg-4oq.14.1): versions, release files, the state header's version.
add_executable(releasetest EXCLUDE_FROM_ALL src/system/test/releasetest.c src/system/release.c
    src/system/statefile.c src/system/statecodec.c src/system/settings.c)
# A release's update manifest and its Ed25519 signature (update/update_manifest.c, xg-4oq.14.2;
# Monocypher, port/third_party/monocypher): RFC 8032's vectors, a signed fixture, refusals.
add_executable(updatemanifesttest EXCLUDE_FROM_ALL src/update/test/updatemanifesttest.c src/update/update_manifest.c
    src/system/release.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_include_directories(updatemanifesttest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher")
# The update check (update/update_check.c, xg-4oq.14.3) with a fake fetcher: the setting, the daily
# rule, a manifest judged (newer, latest, skipped, switched off, untrusted), no request when off.
# With "--fetch URL" it fetches through this system's own fetcher (update_fetch.c) instead.
add_executable(updatechecktest EXCLUDE_FROM_ALL src/update/test/updatechecktest.c src/update/update_check.c
    src/update/update_fetch.c src/update/update_manifest.c src/system/release.c src/system/settings.c
    third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_include_directories(updatechecktest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher")
if(WIN32)
    target_link_libraries(updatechecktest PRIVATE winhttp)
endif()
# The one-click update (update/update_install.c, update_archive.c with puff, xg-4oq.14.4): zip and
# tar.gz read and refused, the download checked, files unpacked beside a copy and swapped, undone,
# finished after a crash, tidied; with a fake fetcher and download, in a folder of its own.
# "--unpack ARCHIVE" lists an archive as the updater reads it (tools/test_update_install.py).
add_executable(updateinstalltest EXCLUDE_FROM_ALL src/update/test/updateinstalltest.c src/update/update_install.c
    src/update/update_archive.c src/update/update_check.c src/update/update_fetch.c src/update/update_manifest.c
    src/system/release.c src/system/settings.c src/system/engine.c third_party/puff/puff.c
    third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_include_directories(updateinstalltest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/puff")
if(WIN32)
    target_link_libraries(updateinstalltest PRIVATE winhttp)
endif()
add_executable(seamreusetest EXCLUDE_FROM_ALL src/gpu/test/seamreusetest.c)
add_executable(spansteptest EXCLUDE_FROM_ALL src/gpu/test/spansteptest.c)
add_executable(battlepacingtest EXCLUDE_FROM_ALL src/system/test/battlepacingtest.c
    src/system/libetc.c src/system/speedup.c src/system/settings.c)
# hw.c's host presentation budget on the real clock (includes hw.c whole).
add_executable(hostpolltest EXCLUDE_FROM_ALL src/system/test/hostpolltest.c)
# The game's identity in a save's free tail (gameid.c).
add_executable(gameidtest EXCLUDE_FROM_ALL src/system/test/gameidtest.c src/system/gameid.c)
# Saves backup (system/backup.c): a folder into a .zip and back; damaged archives refused.
add_executable(backuptest EXCLUDE_FROM_ALL src/system/test/backuptest.c src/system/backup.c)
# Battle speed (xg-mrj.2): hw.c's audio clock, libapi.c's root counter 2 and
# battlespeed.c's decisions (includes the three whole); gameplay.c's enemy-info
# Select rule beside its chord.
add_executable(battlespeedtest EXCLUDE_FROM_ALL src/system/test/battlespeedtest.c src/system/settings.c
    src/system/gameplay.c)
# The stall watchdog in the app's background (stall.c, xg-mrj.3.19): SDL's
# lifecycle events, polls without a thread or a clock.
add_executable(stallwatchtest EXCLUDE_FROM_ALL src/system/test/stallwatchtest.c src/system/stall.c)
# The dialogue backlog's capture (backlog.c, xg-htn.57): the printer's calls
# as dialog.c makes them, the screens, captions, its controls.
add_executable(backlogtest EXCLUDE_FROM_ALL src/system/test/backlogtest.c src/system/backlog.c src/system/settings.c)
foreach(tool rewindpacktest statefiletest releasetest updatemanifesttest updatechecktest updateinstalltest seamreusetest spansteptest battlepacingtest hostpolltest gameidtest battlespeedtest backuptest stallwatchtest backlogtest)
    target_include_directories(${tool} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
    target_compile_definitions(${tool} PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
    target_compile_options(${tool} PRIVATE -fno-strict-aliasing)
    target_link_libraries(${tool} PRIVATE SDL3::SDL3 m)
endforeach()
# These two include gp0.c whole and link only what their checks reach:
# --gc-sections drops the functions whose callees (Vulkan, texture, VRAM
# layers) are undefined. Load-bearing: GNU ld for ELF and lld only resolve the
# sections they keep, GNU ld for PE (MinGW) resolves every input section, so
# these link on ELF hosts only. rewindpacktest and battlepacingtest define
# their unreached callees instead and link everywhere without it.
foreach(tool seamreusetest spansteptest)
    target_compile_options(${tool} PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(${tool} PRIVATE -Wl,--gc-sections)
endforeach()
add_executable(gte_precisiontest EXCLUDE_FROM_ALL src/gte/test/precisiontest.c src/gte/gte.c src/gte/precision.c)
target_include_directories(gte_precisiontest PRIVATE "${XG_GAME_ROOT}/include")
target_compile_definitions(gte_precisiontest PRIVATE PLATFORM_PC)
target_link_libraries(gte_precisiontest PRIVATE m)
add_executable(model_geometrytest EXCLUDE_FROM_ALL src/game/main/test/geometrytest.c
    src/game/main/render.c src/gte/gte.c src/gte/precision.c)
target_include_directories(model_geometrytest PRIVATE "${XG_GAME_ROOT}/include")
target_compile_definitions(model_geometrytest PRIVATE PLATFORM_PC)
target_compile_options(model_geometrytest PRIVATE -fno-strict-aliasing)
target_link_libraries(model_geometrytest PRIVATE m)
add_executable(thin_geometrytest EXCLUDE_FROM_ALL src/game/main/test/thin_geometrytest.c
    src/game/battling/ground.c src/game/battling/shadow.c src/game/worldmap/ground.c
    "${XG_GAME_ROOT}/src/main/light.c" "${XG_GAME_ROOT}/src/battle/b_800B15D8.c"
    src/system/memory.c src/gte/libgte.c src/gte/gte.c src/gte/precision.c)
target_include_directories(thin_geometrytest PRIVATE "${XG_GAME_ROOT}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(thin_geometrytest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_compile_options(thin_geometrytest PRIVATE -fno-strict-aliasing -ffunction-sections -fdata-sections
    $<$<PLATFORM_ID:Windows>:-mno-ms-bitfields;-mcmodel=small;-fno-asynchronous-unwind-tables;-fno-unwind-tables>)
target_link_options(thin_geometrytest PRIVATE -Wl,--gc-sections)
target_link_libraries(thin_geometrytest PRIVATE m)
add_executable(slidestacktest EXCLUDE_FROM_ALL src/game/field/test/slidestacktest.c
    src/game/field/slidestack.c src/game/main/retailheap.c)
target_include_directories(slidestacktest PRIVATE "${XG_GAME_ROOT}/include")
target_compile_definitions(slidestacktest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
add_executable(retailheaptest EXCLUDE_FROM_ALL src/game/main/test/retailheaptest.c)
target_include_directories(retailheaptest PRIVATE "${XG_GAME_ROOT}/include")
target_compile_definitions(retailheaptest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
add_executable(gallery_mediatest EXCLUDE_FROM_ALL src/extras/test/mediatest.c src/extras/gallery_refs.c)
target_include_directories(gallery_mediatest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(gallery_battletest EXCLUDE_FROM_ALL src/extras/test/battletest.c src/extras/gallery_battle.c)
target_include_directories(gallery_battletest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(gallery_scenetest EXCLUDE_FROM_ALL src/extras/test/scenetest.c
    src/extras/gallery_scene.c src/system/options.c)
target_include_directories(gallery_scenetest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(gallery_scenetest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_link_libraries(gallery_scenetest PRIVATE SDL3::SDL3)

# Gameplay tests drive real game C with synthetic state. xg_gameplay_code
# compiles game files like the game; xg_gameplay_test links a test with the
# gameplay and settings code and, off Windows, the GAME object libraries and
# HOST sources it needs (--gc-sections drops the rest of each overlay).
function(xg_gameplay_code name)
    add_library(${name} OBJECT EXCLUDE_FROM_ALL ${ARGN})
    target_include_directories(${name} PRIVATE "${XG_GAME_ROOT}/include")
    target_compile_definitions(${name} PRIVATE PLATFORM_PC VERSION_${XG_VERSION} _LANGUAGE_C)
    target_compile_options(${name} PRIVATE ${XG_GAME_OPTIONS} -ffunction-sections -fdata-sections
        $<$<BOOL:${WIN32}>:-fno-asynchronous-unwind-tables;-fno-unwind-tables>)
endfunction()

function(xg_gameplay_test name)
    cmake_parse_arguments(T "" "" "SOURCES;GAME;HOST" ${ARGN})
    add_executable(${name} EXCLUDE_FROM_ALL ${T_SOURCES} src/system/gameplay.c src/system/settings.c)
    if(NOT WIN32)
        foreach(lib IN LISTS T_GAME)
            target_sources(${name} PRIVATE $<TARGET_OBJECTS:${lib}>)
        endforeach()
        target_sources(${name} PRIVATE ${T_HOST})
    endif()
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
    target_compile_definitions(${name} PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
    target_compile_options(${name} PRIVATE -fno-pie ${XG_WIN32_LAYOUT_OPTIONS})
    target_link_options(${name} PRIVATE -Wl,--gc-sections $<$<NOT:$<BOOL:${WIN32}>>:-no-pie>)
    target_link_libraries(${name} PRIVATE SDL3::SDL3)
endfunction()

# The field/world-map encounter countdowns.
xg_gameplay_code(gameplay_encounter_code "${XG_GAME_ROOT}/src/field/f_80078B5C.c" "${XG_GAME_ROOT}/src/worldmap/w_800736DC.c")
xg_gameplay_test(gameplay_encountertest SOURCES src/system/test/gameplaytest.c GAME gameplay_encounter_code)

# Enemy stats at battle load and their native HP damage/healing paths.
xg_gameplay_code(gameplay_combat_code "${XG_GAME_ROOT}/src/battle_loader/bld_801E4048.c"
    "${XG_GAME_ROOT}/src/battle/b_80085350.c" "${XG_GAME_ROOT}/src/battle/b_80089B50.c"
    "${XG_GAME_ROOT}/src/battle/b_80079674.c" "${XG_GAME_ROOT}/src/battle/b_800769E8.c")
xg_gameplay_test(gameplay_combattest SOURCES src/system/test/combattest.c GAME gameplay_combat_code)

# EXP and gold on the result screen.
xg_gameplay_code(gameplay_reward_code "${XG_GAME_ROOT}/src/battle_result/br_801DE5C4.c")
xg_gameplay_test(gameplay_rewardtest SOURCES src/system/test/rewardtest.c GAME gameplay_reward_code)

# Deathblow practice in the native attack resolution.
xg_gameplay_code(gameplay_deathblow_code "${XG_GAME_ROOT}/src/battle/b_800941A4.c")
xg_gameplay_test(gameplay_deathblowtest SOURCES src/system/test/deathblowtest.c GAME gameplay_deathblow_code)

# Dialogue speed, hold and auto-advance settings.
xg_gameplay_test(gameplay_dialoguetest SOURCES src/system/test/dialoguetest.c)

# The stored field60slow setting the options and presets read (xg-dzg).
xg_gameplay_test(gameplay_field60slowtest SOURCES src/system/test/field60slowtest.c)

# The retry setting's checkpoint of a battle.
xg_gameplay_code(gameplay_retry_code "${CMAKE_CURRENT_SOURCE_DIR}/src/game/main/retry.c")
xg_gameplay_test(gameplay_retrytest SOURCES src/system/test/retrytest.c GAME gameplay_retry_code)

# Gauntlets: the run's plan, settings and records, and the game's side.
xg_gameplay_code(gameplay_gauntlet_code "${CMAKE_CURRENT_SOURCE_DIR}/src/game/main/gauntlet.c")
xg_gameplay_test(gameplay_gauntlettest SOURCES src/system/test/gauntlettest.c src/system/gauntlet.c
    GAME gameplay_gauntlet_code)

# The original bug fixes in the gear shop, menus and battle.
xg_gameplay_code(gameplay_bugfix_code "${XG_GAME_ROOT}/src/menu_gear_shop/gs_801D1304.c")
xg_gameplay_code(gameplay_bugfix_save_code "${XG_GAME_ROOT}/src/menu/m_801E3088.c")
# The two overlays normally keep identically named helpers local to each module.
foreach(helper applyGearAccessoryBonuses applyGearArmorStats applyGearEngineStats applyGearFrameStats
        applyGearWeaponStats computeGearAgilityPenalty computeGearStatusStats)
    target_compile_definitions(gameplay_bugfix_save_code PRIVATE ${helper}=save_${helper})
endforeach()
xg_gameplay_code(gameplay_bugfix_battle_code "${XG_GAME_ROOT}/src/battle/b_800941A4.c" "${XG_GAME_ROOT}/src/battle/b_800B7C28.c")
xg_gameplay_code(gameplay_bugfix_equip_code "${XG_GAME_ROOT}/src/menu/m_801DE29C.c")
xg_gameplay_code(gameplay_bugfix_shop_code "${XG_GAME_ROOT}/src/menu_shop/sh_801C5040.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/field/sfxkeep.c")
foreach(helper initMenuContext_1DE0 setupMenuContext364 setupMenuContext4E0)
    target_compile_definitions(gameplay_bugfix_shop_code PRIVATE ${helper}=shop_${helper})
endforeach()
xg_gameplay_code(gameplay_bugfix_party_code "${XG_GAME_ROOT}/src/menu_party/pt_801C5018.c")
# The party overlay carries its own copy of the menu library the shop's has.
foreach(helper computeMenuBorders disableDraw428 draw380 drawMenu2_sub drawMenu2_sub_sub0 drawMenu2_sub_sub1
        drawMenu2_sub_sub2 drawMenuBackground drawMenuBoxBackground drawMenuBoxBottomFrame drawMenuBoxCorners
        drawMenuBoxInnerFrame drawMenuBoxLeftFrame drawMenuBoxRightFrame drawMenuBoxTopFrame drawMenuBoxes
        drawMenuSpecificElements drawMenu_380 drawMenu_428 fillStringDisplayBuffer iniMenuContext364And380
        initMemoryCardPolyVerts initMenuContext32C initMenuContext330 initMenuContext33C initMenuContext348
        initMenuContext350MainMenu initMenuContext354 initMenuContext4E0 initMenuContext_1DE0 initMenuGlobals
        initMenuTiles1 initMenuTiles1Sub initMenuTiles4 initMenuTiles5 initMenuTiles6 initMenuTilesBottomFrame
        initMenuTilesTopFrame initPrimitives_348_cursor loadBlankImage loadMenuSharedResources menu2CheckBitmask
        menuDraw playMenuSoundEffect processLoadSaveMenuSub7Sub0 resetCursorState resetMenuOddOrEven setupDraw428
        setupMenuContext364 setupMenuContext4E0 setupPolyG4 updateMenuInput)
    target_compile_definitions(gameplay_bugfix_party_code PRIVATE ${helper}=party_${helper})
endforeach()
# The field-script corrections (the test stubs the variables and music calls).
xg_gameplay_code(gameplay_bugfix_field_code "${CMAKE_CURRENT_SOURCE_DIR}/src/game/field/scriptfix.c")
xg_gameplay_test(gameplay_bugfixtest SOURCES src/system/test/bugfixtest.c
    GAME gameplay_bugfix_code gameplay_bugfix_save_code gameplay_bugfix_battle_code gameplay_bugfix_equip_code
    gameplay_bugfix_shop_code gameplay_bugfix_party_code gameplay_bugfix_field_code HOST src/gpu/prim.c)
target_compile_options(gameplay_bugfixtest PRIVATE -ffunction-sections -fdata-sections)

# Dialogue portraits.
xg_gameplay_code(gameplay_portrait_code "${XG_GAME_ROOT}/src/field/f_8009BB0C.c" "${XG_GAME_ROOT}/src/field/f_8006FDEC.c"
    "${XG_GAME_ROOT}/src/field/f_8007DCF8.c")
xg_gameplay_test(gameplay_portraittest SOURCES src/system/test/portraittest.c src/system/portraitfix.c
    src/game/main/retailheap.c GAME gameplay_portrait_code HOST src/gpu/prim.c)
target_compile_options(gameplay_portraittest PRIVATE -ffunction-sections -fdata-sections)

foreach(module IN LISTS XG_MODULES)
    xg_game_objects(${module})
endforeach()

# Field speed (xg-mrj.6, tools/test_field_speed.py): fieldspeed.c, battlespeed.c's
# vblank on hw.c's clock (the test includes those two whole) and the field's
# own movie calls in game/field/wide.c, compiled as the game's. The rest of
# wide.c reaches the renderer: --gc-sections drops it, so this links on ELF
# hosts only (as seamreusetest and spansteptest).
xg_gameplay_code(fieldspeedtest_wide "${CMAKE_CURRENT_SOURCE_DIR}/src/game/field/wide.c")
add_executable(fieldspeedtest EXCLUDE_FROM_ALL src/system/test/fieldspeedtest.c src/system/fieldspeed.c
    src/system/settings.c $<TARGET_OBJECTS:fieldspeedtest_wide>)
target_include_directories(fieldspeedtest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(fieldspeedtest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_compile_options(fieldspeedtest PRIVATE -fno-pie -fno-strict-aliasing -ffunction-sections -fdata-sections)
target_link_options(fieldspeedtest PRIVATE -Wl,--gc-sections -no-pie)
target_link_libraries(fieldspeedtest PRIVATE SDL3::SDL3 m)

# Every module is linked into the executable. tools/pc_link.py generates what
# the game C does not define on the PC: stubs for missing functions (stubs.c),
# each module's data that lives in the PS1 build's assembly (<module>.c, from
# the player's disc, so it stays in the build directory), each module's export
# list, and the overlay table (modules.c). Each module then becomes one
# partially linked object whose symbols are local except its exports, and each
# overlay's data and BSS get their own sections so the port can reset them when
# the game "loads" the overlay again.
# The platform layer: port/src (entry, defaults) and its subsystems. It may use
# the game's types (include/, PLATFORM_PC) next to system headers.
file(GLOB XG_PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/system/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cd/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gte/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/mdec/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/spu/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/card/*.c")
add_library(platform OBJECT ${XG_PLATFORM_SOURCES})
target_compile_features(platform PRIVATE c_std_11)
target_include_directories(platform PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
# XG_ROOT, the repository's folder, only in a development build (XG_DIST above).
set(XG_ROOT_DEFINE "XG_ROOT=\"${XG_ROOT}\"")
if(XG_DIST)
    set(XG_ROOT_DEFINE "")
endif()
target_compile_definitions(platform PRIVATE PLATFORM_PC VERSION_${XG_VERSION} ${XG_ROOT_DEFINE}
    XG_ENGINE_ENHANCED=${XG_ENGINE_ENHANCED})
# -fno-strict-aliasing: platform code reads the game's packets and buffers
# through the same punned views the game uses.
target_compile_options(platform PRIVATE ${XG_PIC_OPTIONS} -fno-strict-aliasing ${XG_WIN32_LAYOUT_OPTIONS})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(platform PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
target_link_libraries(platform PRIVATE SDL3::SDL3)
# RetroArch .slang/.slangp shader presets (port/src/gpu/shaderchain.c) are
# compiled at run time by Khronos glslang, an optional component: off
# (-DXG_GLSLANG=OFF) or not found, a preset is refused with a note.
option(XG_GLSLANG "Shader presets: link Khronos glslang" ON)
set(XG_GLSLANG_LIBS)
set(XG_GLSLANG_LICENSE "")
if(XG_GLSLANG)
    find_package(glslang CONFIG QUIET)
    if(glslang_FOUND)
        # Some glslang packages reference the optimizer without importing its
        # target. Import it so the link also gets the base SPIRV-Tools library.
        get_target_property(glslang_dependencies glslang::glslang INTERFACE_LINK_LIBRARIES)
        if("SPIRV-Tools-opt" IN_LIST glslang_dependencies AND NOT TARGET SPIRV-Tools-opt)
            find_package(SPIRV-Tools-opt CONFIG REQUIRED)
        endif()
        set(XG_GLSLANG_LIBS glslang::glslang glslang::glslang-default-resource-limits)
        set(XG_GLSLANG_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glslang/LICENSE.txt")
        target_compile_definitions(platform PRIVATE XG_HAVE_GLSLANG=1)
        target_link_libraries(platform PRIVATE ${XG_GLSLANG_LIBS})
    else()
        message(STATUS "glslang not found: no shader presets (tools/build_glslang_mingw.sh for Windows)")
    endif()
endif()
# AV1 videos in movie packs (port/src/gpu/av1video.c) are decoded by dav1d,
# an optional component: off (-DXG_DAV1D=OFF) or not found, a pack's .webm
# movies are skipped with a note (its PNG frames still work).
option(XG_DAV1D "Movie pack videos: link dav1d (AV1)" ON)
set(XG_DAV1D_LIBS)
set(XG_DAV1D_LICENSE "")
if(XG_DAV1D)
    find_path(XG_DAV1D_INCLUDE dav1d/dav1d.h)
    find_library(XG_DAV1D_LIBRARY NAMES dav1d)
    if(XG_DAV1D_INCLUDE AND XG_DAV1D_LIBRARY)
        set(XG_DAV1D_LIBS "${XG_DAV1D_LIBRARY}")
        set(XG_DAV1D_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dav1d/COPYING")
        target_compile_definitions(platform PRIVATE XG_HAVE_DAV1D=1)
        target_include_directories(platform PRIVATE "${XG_DAV1D_INCLUDE}")
        target_link_libraries(platform PRIVATE ${XG_DAV1D_LIBS})
    else()
        message(STATUS "dav1d not found: no AV1 movie videos (tools/build_dav1d_mingw.sh for Windows)")
    endif()
endif()
# port/src/gpu/gpu.cmake (optional): Vulkan, shaders.
include("${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/gpu.cmake" OPTIONAL)

# The settings menu (port/src/ui) drawn with Dear ImGui (port/third_party/imgui,
# unmodified): C++ for these files only; the game and the platform layer stay C.
enable_language(CXX)
set(XG_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
file(GLOB XG_UI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp")
add_library(ui OBJECT ${XG_UI_SOURCES}
    "${XG_IMGUI_DIR}/imgui.cpp" "${XG_IMGUI_DIR}/imgui_draw.cpp" "${XG_IMGUI_DIR}/imgui_tables.cpp"
    "${XG_IMGUI_DIR}/imgui_widgets.cpp" "${XG_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${XG_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp")
target_compile_features(ui PRIVATE cxx_std_17)
add_executable(ui_motiontest EXCLUDE_FROM_ALL src/ui/test/motiontest.cpp src/ui/motion.cpp
    "${XG_IMGUI_DIR}/imgui.cpp" "${XG_IMGUI_DIR}/imgui_draw.cpp"
    "${XG_IMGUI_DIR}/imgui_tables.cpp" "${XG_IMGUI_DIR}/imgui_widgets.cpp")
target_compile_features(ui_motiontest PRIVATE cxx_std_17)
target_include_directories(ui_motiontest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_IMGUI_DIR}")
# Japanese line breaking in the menus' text (xg-em4y): no ImGui, no SDL.
add_executable(ui_textwraptest EXCLUDE_FROM_ALL src/ui/test/textwraptest.cpp src/ui/textwrap.cpp)
target_compile_features(ui_textwraptest PRIVATE cxx_std_17)
target_include_directories(ui_textwraptest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_include_directories(ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_IMGUI_DIR}" "${XG_IMGUI_DIR}/backends"
    "${CMAKE_BINARY_DIR}/gen" "${XG_GAME_ROOT}/include")
# The Vulkan functions come from SDL's loader (as in vk.c); clipboard and the
# shell through SDL on Windows too.
target_compile_definitions(ui PRIVATE PLATFORM_PC VERSION_${XG_VERSION} XG_ENGINE_ENHANCED=${XG_ENGINE_ENHANCED} IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    $<$<BOOL:${WIN32}>:IMGUI_DISABLE_WIN32_FUNCTIONS>)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${XG_UI_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter")
endif()
target_link_libraries(ui PRIVATE SDL3::SDL3 Vulkan::Headers)
# The opt-in update check (port/src/update, xg-4oq.14.3): the question before the game, F1 >
# System > Updates, the system's own HTTPS (update_fetch.c: WinHTTP, Android's HttpURLConnection,
# libcurl loaded at run time). Off by default, and in releases, until a release's save states load
# both ways in every other canonical release's (xg-4oq.14.7); without it nothing of it is built.
option(XG_UPDATER "the opt-in update check (port/src/update; off until xg-4oq.14.7)" OFF)
if(XG_UPDATER)
    target_sources(platform PRIVATE src/update/update_check.c src/update/update_fetch.c
        src/update/update_manifest.c src/update/update_install.c src/update/update_archive.c
        third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c third_party/puff/puff.c)
    target_include_directories(platform PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/puff")
    # Vendored as they are (their READMEs): GCC's warnings there are for upstream, not for this build.
    set_source_files_properties(third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c
        third_party/puff/puff.c PROPERTIES COMPILE_OPTIONS "-w")
    # A test build of the updater, never a release: the tests' FIXTURE key trusted too, and with
    # XG_UPDATE_TEST_DIR set, the release's files read from that folder instead of the network: an
    # install run end to end on files made locally (docs: port/src/update/update_check.c).
    option(XG_UPDATER_TEST "a test build of the updater (the FIXTURE key, XG_UPDATE_TEST_DIR): never a release" OFF)
    if(XG_UPDATER_TEST)
        if(XG_DIST)
            message(FATAL_ERROR "XG_UPDATER_TEST with XG_DIST: a build to hand out never trusts the test key")
        endif()
        target_compile_definitions(platform PRIVATE XG_UPDATER_TEST=1)
    endif()
    target_compile_definitions(platform PRIVATE XG_UPDATER=1)
    target_compile_definitions(ui PRIVATE XG_UPDATER=1)
endif()
# The About section's build line, made again at each build (port/cmake/buildinfo.cmake);
# an Android build ships the NDK's C++ runtime, whose licence About shows too.
set(XG_LIBCXX_LICENSE "")
if(ANDROID)
    set(XG_LIBCXX_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llvm-libcxx/LICENSE.txt")
endif()
add_custom_target(ui_buildinfo
    COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_BINARY_DIR}/gen/ui_buildinfo.h -DROOT=${XG_ROOT}
            -DVERSION=${XG_VERSION} "-DTYPE=${CMAKE_BUILD_TYPE}${XG_BUILD_TYPE_SUFFIX}" -DIMGUI_LICENSE=${XG_IMGUI_DIR}/LICENSE.txt
            -DSDL_LICENSE=${SDL3_DIR}/../../../share/licenses/SDL3/LICENSE.txt
            "-DGLSLANG_LICENSE=${XG_GLSLANG_LICENSE}" "-DDAV1D_LICENSE=${XG_DAV1D_LICENSE}"
            "-DLIBCXX_LICENSE=${XG_LIBCXX_LICENSE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/buildinfo.cmake"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/gen/ui_buildinfo.h"
    VERBATIM)
add_dependencies(ui ui_buildinfo)

# The extras (port/src/extras/extras.cmake): their C joins the platform layer,
# their C++ the menu's library; their own shaders.
include("${CMAKE_CURRENT_SOURCE_DIR}/src/extras/extras.cmake" OPTIONAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Pixel fallback for Japanese characters absent from the player's native
# font. Normal builds need no system fonts or external font tools.
set(XG_JUKEBOX_FONT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jukebox-font/glyphs.bin")
set(XG_JUKEBOX_FONT_HEADER "${CMAKE_BINARY_DIR}/gen/jukebox_font.h")
add_custom_command(OUTPUT "${XG_JUKEBOX_FONT_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${XG_ROOT}/tools/jukebox_font.py" embed
        "${XG_JUKEBOX_FONT}" "${XG_JUKEBOX_FONT_HEADER}"
    DEPENDS "${XG_JUKEBOX_FONT}" "${XG_ROOT}/tools/jukebox_font.py"
    VERBATIM)
add_custom_target(ui_jukebox_font DEPENDS "${XG_JUKEBOX_FONT_HEADER}")
add_dependencies(ui ui_jukebox_font)
# The interface's built-in languages (port/src/ui/l10n.h): the PO catalogs in
# port/src/ui/lang as C++ data (standard library Python only).
file(GLOB XG_L10N_CATALOGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/lang/*.po")
set(XG_L10N_HEADER "${CMAKE_BINARY_DIR}/gen/l10n_catalogs.h")
add_custom_command(OUTPUT "${XG_L10N_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${XG_ROOT}/tools/l10n.py" embed "${XG_L10N_HEADER}" ${XG_L10N_CATALOGS}
    DEPENDS ${XG_L10N_CATALOGS} "${XG_ROOT}/tools/l10n.py"
    VERBATIM)
add_custom_target(ui_l10n DEPENDS "${XG_L10N_HEADER}")
add_dependencies(ui ui_l10n)
# Netplay's compatibility id (tools/netplay_compat.py): a hash of the sources that decide the game,
# made again at each build, so a Linux and a Windows build of the same game play together.
add_custom_target(netplay_compat
    COMMAND "${Python3_EXECUTABLE}" "${XG_ROOT}/tools/netplay_compat.py" "${CMAKE_BINARY_DIR}/gen/netplay_compat.h"
            ${XG_VERSION} ${XG_ENGINE}
    BYPRODUCTS "${CMAKE_BINARY_DIR}/gen/netplay_compat.h"
    VERBATIM)
add_dependencies(platform netplay_compat)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/system/netplay.c" APPEND PROPERTY INCLUDE_DIRECTORIES
    "${CMAKE_BINARY_DIR}/gen")
# The game's text from another source (system/gametext.h): fonts, codes, packs; a US disc's round trip.
add_executable(gametexttest EXCLUDE_FROM_ALL src/system/test/gametexttest.c src/system/gametext_font.c
    src/system/gametext_disc.c src/system/gametext_pack.c src/system/gametext_member.c src/system/gametext_sprite.c
    src/system/gametext_picture.c src/cd/verify.c src/cd/disc.c src/cd/swap.c
    src/system/settings.c)
target_include_directories(gametexttest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(gametexttest PRIVATE PLATFORM_PC)
target_link_libraries(gametexttest PRIVATE SDL3::SDL3)
# Bounded authored sprite placement, including aliased records and atomic fallback.
add_executable(gametext_spritetest EXCLUDE_FROM_ALL src/system/test/gametext_spritetest.c
    src/system/gametext_sprite.c)
target_include_directories(gametext_spritetest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(ui_l10ntest EXCLUDE_FROM_ALL src/ui/test/l10ntest.cpp src/ui/l10n.cpp src/system/settings.c)
target_compile_features(ui_l10ntest PRIVATE cxx_std_17)
target_include_directories(ui_l10ntest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/gen")
target_compile_definitions(ui_l10ntest PRIVATE PLATFORM_PC)
target_link_libraries(ui_l10ntest PRIVATE SDL3::SDL3)
add_dependencies(ui_l10ntest ui_l10n)
if(APPLE)
    xg_darwin_linker()
    set(XG_MODULE_FORMAT --darwin)
else()
    find_program(XG_LD NAMES ld REQUIRED)
    find_program(XG_OBJCOPY NAMES objcopy REQUIRED)
endif()
# pc_link's layout probes (a cross build's toolchain file names the host gcc).
if(NOT XG_PROBE_CC)
    set(XG_PROBE_CC "${CMAKE_C_COMPILER}")
endif()
string(TOLOWER "${XG_VERSION}" XG_VERSION_LOWER)
set(XG_GEN_DIR "${CMAKE_BINARY_DIR}/gen")
set(XG_ALL_OBJECTS)
set(XG_GEN_OUTPUTS "${XG_GEN_DIR}/stubs.c" "${XG_GEN_DIR}/modules.c")
foreach(module IN LISTS XG_MODULES)
    list(APPEND XG_ALL_OBJECTS $<TARGET_OBJECTS:game_${module}>)
    list(APPEND XG_GEN_OUTPUTS "${XG_GEN_DIR}/${module}.c" "${XG_GEN_DIR}/${module}.exports")
endforeach()
# pc_link also reads the PS1 split (asm/<ver>, rewritten by tools/configure.py,
# which rewrites build.ninja too) and the symbol files: rerun when they change.
file(GLOB XG_PCLINK_INPUTS CONFIGURE_DEPENDS
    "${XG_ROOT}/config/${XG_VERSION_LOWER}/symbols/*.txt"
    "${XG_ROOT}/config/${XG_VERSION_LOWER}/*.yaml")
if(EXISTS "${XG_ROOT}/build.ninja")
    list(APPEND XG_PCLINK_INPUTS "${XG_ROOT}/build.ninja")
endif()

# What the link takes from the disc (xg-4oq.8): which of the game's data is
# zero, where its pointers are, and where on the disc each copied byte is.
#   disc    from the PS1 split (asm/<ver>), the extracted images (disc/<ver>)
#           and the PS1 build (build/<ver>), as tools/extract.py,
#           tools/configure.py and ninja leave them.
#   tables  from port/link-tables: the same facts as files (addresses, sizes,
#           offsets and hashes, no game byte), so a checkout without a disc
#           links. They are one region's, one engine's and one platform's,
#           and the source data plans one compiler's: a build they were not
#           made for stops with what differs. The executable reads the game
#           from the player's disc at every start either way.
#   auto    the disc where asm/<ver> is, else the tables.
# XG_WRITE_LINK_TABLES writes a disc-backed build's tables to
# <build>/link-tables; tools/link_tables.py gathers builds' tables into
# port/link-tables and checks them.
set(XG_LINK_FROM "auto" CACHE STRING "What the link reads: auto, disc or tables")
set_property(CACHE XG_LINK_FROM PROPERTY STRINGS auto disc tables)
set(XG_LINK_TABLES "${CMAKE_CURRENT_SOURCE_DIR}/link-tables" CACHE PATH "The link tables (XG_LINK_FROM=tables)")
option(XG_WRITE_LINK_TABLES "Write this disc-backed build's link tables to <build>/link-tables" OFF)
string(TOLOWER "${XG_ENGINE}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" XG_TABLES_CONFIG)
set(xg_link_from "${XG_LINK_FROM}")
if(xg_link_from STREQUAL "auto")
    if(EXISTS "${XG_ROOT}/asm/${XG_VERSION_LOWER}")
        set(xg_link_from disc)
    else()
        set(xg_link_from tables)
    endif()
endif()
set(XG_LINK_INPUTS)
set(XG_LINK_TABLE_FILES)
if(xg_link_from STREQUAL "tables")
    if(XG_WRITE_LINK_TABLES)
        message(FATAL_ERROR "XG_WRITE_LINK_TABLES: the tables are written from a disc-backed link (XG_LINK_FROM=disc)")
    endif()
    set(tables "${XG_LINK_TABLES}/${XG_VERSION_LOWER}")
    set(XG_MODULE_PLAN_DIR "${tables}/${XG_TABLES_CONFIG}/srcdata")
    set(XG_LINK_TABLE_FILES "${tables}/${XG_TABLES_CONFIG}/facts.json" "${tables}/asm-layout.json")
    foreach(file IN LISTS XG_LINK_TABLE_FILES)
        if(NOT EXISTS "${file}")
            message(FATAL_ERROR "No link table ${file}.\n"
                "This checkout has no PS1 split (asm/${XG_VERSION_LOWER}) and no tables for ${XG_VERSION} "
                "${XG_TABLES_CONFIG}. Build from your disc (tools/extract.py, tools/configure.py, ninja: "
                "docs/building.md), or extract a release's link tables (its link-tables.tar.gz) as "
                "port/link-tables and build a configuration they were made for.")
        endif()
    endforeach()
    list(GET XG_LINK_TABLE_FILES 0 facts)
    list(GET XG_LINK_TABLE_FILES 1 asm_layout)
    set(XG_LINK_INPUTS --facts "${facts}" --asm-layout "${asm_layout}")
    # The PS1 object facts are there when the tables were made beside a PS1 build (the release's builder has none).
    if(EXISTS "${tables}/${XG_TABLES_CONFIG}/ps1-objects.json")
        list(APPEND XG_LINK_TABLE_FILES "${tables}/${XG_TABLES_CONFIG}/ps1-objects.json")
        list(APPEND XG_LINK_INPUTS --ps1-objects "${tables}/${XG_TABLES_CONFIG}/ps1-objects.json")
    endif()
    message(STATUS "Link from the tables in ${tables} (${XG_TABLES_CONFIG}): no disc is read")
    # The source data plans fit one compiler's objects (tools/pc_srcdata.py): say so before the build does.
    if(EXISTS "${tables}/${XG_TABLES_CONFIG}/compiler.txt")
        file(STRINGS "${tables}/${XG_TABLES_CONFIG}/compiler.txt" made_with LIMIT_COUNT 1)
        if(NOT made_with STREQUAL "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
            message(WARNING "The link tables were made with ${made_with}; this compiler is ${CMAKE_C_COMPILER_ID} "
                "${CMAKE_C_COMPILER_VERSION}. Where it lays an object's data out differently, the build stops at a "
                "stale source data plan: build in the release's builder (build-release.sh) or from your disc.")
        endif()
    endif()
elseif(xg_link_from STREQUAL "disc")
    if(NOT EXISTS "${XG_ROOT}/asm/${XG_VERSION_LOWER}")
        message(FATAL_ERROR "XG_LINK_FROM=disc: no PS1 split in ${XG_ROOT}/asm/${XG_VERSION_LOWER} "
            "(tools/extract.py, tools/configure.py, ninja)")
    endif()
    if(XG_WRITE_LINK_TABLES)
        set(XG_WRITE_TABLES_DIR "${CMAKE_BINARY_DIR}/link-tables")
        file(MAKE_DIRECTORY "${XG_WRITE_TABLES_DIR}/srcdata")
        # Which tables these are, for tools/link_tables.py.
        file(WRITE "${XG_WRITE_TABLES_DIR}/config.txt" "${XG_VERSION_LOWER} ${XG_TABLES_CONFIG}\n")
        file(WRITE "${XG_WRITE_TABLES_DIR}/compiler.txt" "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}\n")
        set(XG_LINK_INPUTS --write-facts "${XG_WRITE_TABLES_DIR}/facts.json"
            --write-asm-layout "${XG_WRITE_TABLES_DIR}/asm-layout.json")
        set(XG_LINK_TABLE_BYPRODUCTS BYPRODUCTS "${XG_WRITE_TABLES_DIR}/facts.json"
            "${XG_WRITE_TABLES_DIR}/asm-layout.json")
        # The PS1 object facts come from the PS1 build's images: written where that build stands.
        if(EXISTS "${XG_ROOT}/build/${XG_VERSION_LOWER}/main.elf")
            list(APPEND XG_LINK_INPUTS --write-ps1-objects "${XG_WRITE_TABLES_DIR}/ps1-objects.json")
            list(APPEND XG_LINK_TABLE_BYPRODUCTS "${XG_WRITE_TABLES_DIR}/ps1-objects.json")
        else()
            file(REMOVE "${XG_WRITE_TABLES_DIR}/ps1-objects.json")
        endif()
    endif()
else()
    message(FATAL_ERROR "XG_LINK_FROM must be auto, disc or tables, not ${XG_LINK_FROM}")
endif()
add_custom_command(
    OUTPUT ${XG_GEN_OUTPUTS}
    COMMAND Python3::Interpreter "${XG_ROOT}/tools/pc_link.py" --ver ${XG_VERSION_LOWER}
            ${XG_MODULE_FORMAT} ${XG_LINK_INPUTS} --cc "${XG_PROBE_CC}" --game-root "${XG_GAME_ROOT}"
            "${XG_GEN_DIR}" ${XG_ALL_OBJECTS} --platform $<TARGET_OBJECTS:platform>
    ${XG_LINK_TABLE_BYPRODUCTS}
    DEPENDS ${XG_ALL_OBJECTS} $<TARGET_OBJECTS:platform> "${XG_ROOT}/tools/pc_link.py" "${XG_ROOT}/tools/pc_macho.py"
            ${XG_PCLINK_INPUTS} ${XG_LINK_TABLE_FILES}
    WORKING_DIRECTORY "${XG_ROOT}"
    COMMENT "Generating stubs, data and export lists (tools/pc_link.py)"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

set(XG_MODULE_OBJECTS)
set(XG_SRCDATA_SOURCES)
foreach(module IN LISTS XG_MODULES)
    xg_link_module(${module})
    set(out "${CMAKE_BINARY_DIR}/modules/${module}.o")
    list(APPEND XG_MODULE_OBJECTS "${out}")
    list(APPEND XG_SRCDATA_SOURCES "${XG_GEN_DIR}/${module}.src.c")
endforeach()
set_source_files_properties(${XG_MODULE_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_library(game_generated OBJECT "${XG_GEN_DIR}/stubs.c" "${XG_GEN_DIR}/modules.c" ${XG_SRCDATA_SOURCES})
target_compile_options(game_generated PRIVATE -w ${XG_PIC_OPTIONS})

# Platform layer and entry point: the executable, or (XG_SHARED) the game
# library xenogears_game (libxenogears.so) and the launcher executable.
if(XG_SHARED)
    set(XG_GAME_TARGET xenogears_game)
    add_library(xenogears_game SHARED
        $<TARGET_OBJECTS:platform>
        $<TARGET_OBJECTS:ui>
        ${XG_MODULE_OBJECTS}
        $<TARGET_OBJECTS:game_generated>
    )
    target_compile_definitions(platform PRIVATE XG_SHARED_LIBRARY=1)
    # Its own definitions bind inside it (as an executable's do), and only the
    # entry is exported (port/cmake/libxenogears.map); nothing left undefined.
    target_link_options(xenogears_game PRIVATE -Wl,-Bsymbolic -Wl,--no-undefined
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/libxenogears.map")
    set_property(TARGET xenogears_game APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libxenogears.map")
    if(ANDROID)
        # The app (port/android): SDL's activity runs libxgloader.so's SDL_main,
        # which loads the region's game library, libxenogears-us.so or -jp.so,
        # at a fixed reserved address (port/android/loader/xgloader.c).
        # Images the activity opened through the Storage Access Framework are read
        # through their descriptors (/proc/self/fd/N): port/src/system/android.c.
        target_link_options(xenogears_game PRIVATE -Wl,--wrap=fopen)
        add_library(xgloader SHARED android/loader/xgloader.c)
        target_link_libraries(xgloader PRIVATE SDL3::SDL3 log dl)
    else()
        add_executable(xenogears src/launcher/launcher.c)
        target_link_libraries(xenogears PRIVATE xenogears_game)
        set_target_properties(xenogears PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    endif()
else()
    set(XG_GAME_TARGET xenogears)
    add_executable(xenogears
        $<TARGET_OBJECTS:platform>
        $<TARGET_OBJECTS:ui>
        ${XG_MODULE_OBJECTS}
        $<TARGET_OBJECTS:game_generated>
    )
endif()
set_target_properties(${XG_GAME_TARGET} PROPERTIES LINKER_LANGUAGE CXX)
if(ANDROID)
    if(NOT XG_SHARED)
        message(FATAL_ERROR "Android builds are shared libraries: -DXG_SHARED=ON (tools/build_android.sh)")
    endif()
    if(XG_ENGINE STREQUAL "enhanced")
        set_target_properties(xenogears_game PROPERTIES OUTPUT_NAME xenogears-enhanced-${XG_VERSION_LOWER})
    else()
        set_target_properties(xenogears_game PROPERTIES OUTPUT_NAME xenogears-${XG_VERSION_LOWER})
    endif()
elseif(XG_ENGINE STREQUAL "enhanced")
    set_target_properties(xenogears PROPERTIES OUTPUT_NAME xenogears-enhanced)
    if(XG_SHARED)
        set_target_properties(xenogears_game PROPERTIES OUTPUT_NAME xenogears-enhanced)
    endif()
elseif(XG_SHARED)
    set_target_properties(xenogears_game PROPERTIES OUTPUT_NAME xenogears)
endif()
# The image's state schema (XG_STATE_SCHEMA above), written after each link of the game. The file
# to ship is named after the image (XG_STATE_SCHEMA_FILE: xenogears.xgschema,
# xenogears-enhanced.xgschema, Android's libxenogears-us.xgschema ...), as the game looks for it
# (port/src/system/stateportable.c XG_STATE_SCHEMA_NAME); the install rules put it where that
# says (Windows: beside the executable; Linux: port/cmake/deb.cmake).
if(XG_STATE_SCHEMA)
    get_target_property(xg_image_name ${XG_GAME_TARGET} OUTPUT_NAME)
    if(NOT xg_image_name)
        set(xg_image_name ${XG_GAME_TARGET})
    endif()
    if(XG_SHARED)
        set(xg_image_name "lib${xg_image_name}")
    endif()
    set(XG_STATE_SCHEMA_FILE "${xg_image_name}.xgschema")
    set(xg_schema_lookup "${XG_STATE_SCHEMA_FILE}")
    if(ANDROID)
        # in the APK's assets, a folder per ABI (tools/build_android.sh puts each library's there)
        set(xg_schema_lookup "schemas/${ANDROID_ABI}/${XG_STATE_SCHEMA_FILE}")
    endif()
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/system/stateportable.c" APPEND PROPERTY
        COMPILE_DEFINITIONS "XG_STATE_SCHEMA_NAME=\"${xg_schema_lookup}\"")
    set(xg_schema_gcc)
    if(XG_SCHEMA_GCC)
        set(xg_schema_gcc --gcc "${XG_SCHEMA_GCC}")
    endif()
    add_custom_command(TARGET ${XG_GAME_TARGET} POST_BUILD
        COMMAND Python3::Interpreter "${XG_ROOT}/tools/state_schema.py" "${CMAKE_BINARY_DIR}"
                --image $<TARGET_FILE:${XG_GAME_TARGET}> --dwarfdump "${XG_DWARFDUMP}" ${xg_schema_gcc}
                --ship "${CMAKE_BINARY_DIR}/${XG_STATE_SCHEMA_FILE}" --quiet
        BYPRODUCTS "${CMAKE_BINARY_DIR}/state_schema.json" "${CMAKE_BINARY_DIR}/state_schema.json.gz"
                   "${CMAKE_BINARY_DIR}/${XG_STATE_SCHEMA_FILE}"
        WORKING_DIRECTORY "${XG_ROOT}"
        COMMENT "Writing the state schema of ${xg_image_name} (tools/state_schema.py)"
        VERBATIM)
    # A change to what the schema is made of writes it again (the link, then the step above).
    set_property(TARGET ${XG_GAME_TARGET} APPEND PROPERTY LINK_DEPENDS "${XG_ROOT}/tools/state_schema.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/system/savestate_inventory.txt")
    if(WIN32)
        install(FILES "${CMAKE_BINARY_DIR}/${XG_STATE_SCHEMA_FILE}" DESTINATION .)
    endif()
endif()
target_link_libraries(${XG_GAME_TARGET} PRIVATE SDL3::SDL3 m ${XG_GLSLANG_LIBS} ${XG_DAV1D_LIBS})
set(XG_ELF_PAYLOAD_MAP OFF CACHE INTERNAL "Use archive provenance in the ELF payload audit" FORCE)
if(XG_DIST AND NOT XG_SHARED AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
        CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND XG_GLSLANG_LIBS)
    get_target_property(glslang_type glslang::glslang TYPE)
    if(glslang_type STREQUAL "STATIC_LIBRARY")
        set(XG_ELF_PAYLOAD_MAP ON CACHE INTERNAL "Use archive provenance in the ELF payload audit" FORCE)
        set(elf_audit_name xenogears)
        if(XG_ENGINE STREQUAL "enhanced")
            set(elf_audit_name xenogears-enhanced)
        endif()
        # Keep the input offsets of string pools intact for the archive audit.
        target_link_options(xenogears PRIVATE -Wl,--unique=.rodata.str*
            "-Wl,-Map,$<TARGET_FILE:xenogears>.map")
        set_property(TARGET xenogears APPEND PROPERTY LINK_DEPENDS
            "${XG_ROOT}/tools/payloadcheck.py" "${XG_ROOT}/tools/payload_elf.py"
            "${XG_ROOT}/tools/payload_pe.py")
        add_custom_command(TARGET xenogears POST_BUILD
            COMMAND Python3::Interpreter "${XG_ROOT}/tools/payloadcheck.py" --record-elf-map
                    $<TARGET_FILE:xenogears> "${CMAKE_STRIP}"
            BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/${elf_audit_name}.map"
                       "${CMAKE_CURRENT_BINARY_DIR}/${elf_audit_name}.payload.json"
            VERBATIM)
    endif()
endif()
if(WIN32)
    # Low addresses as on Linux: a fixed image base far below the scratchpad
    # (0x1F800000), never relocated. Linux's 8 MB main stack. The C and GCC
    # runtimes linked in, so only SDL3.dll ships beside the executable:
    #   cmake --install <build> --prefix <folder>   (tools/build_windows.sh)
    target_link_options(xenogears PRIVATE -Wl,--image-base,0x400000 -Wl,--disable-dynamicbase
        -Wl,--disable-high-entropy-va -Wl,--stack,0x800000 -static
        "-Wl,-Map,$<TARGET_FILE:xenogears>.map")
    # COFF symbols do not carry ELF sizes. Keep exact input placement for
    # the payload audit, with hashes that reject a stale image or map.
    set_property(TARGET xenogears APPEND PROPERTY LINK_DEPENDS
        "${XG_ROOT}/tools/payloadcheck.py" "${XG_ROOT}/tools/payload_pe.py")
    add_custom_command(TARGET xenogears POST_BUILD
        COMMAND Python3::Interpreter "${XG_ROOT}/tools/payloadcheck.py" --record-map $<TARGET_FILE:xenogears>
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/xenogears.exe.map"
                   "${CMAKE_CURRENT_BINARY_DIR}/xenogears.exe.payload.json"
        VERBATIM)
    target_link_libraries(xenogears PRIVATE ws2_32) # netplay's UDP (system/net.c)
    if(XG_UPDATER)
        target_link_libraries(xenogears PRIVATE winhttp) # the update check's HTTPS (update/update_fetch.c)
    endif()
    # The manifest: UTF-8 process code page (paths outside ASCII), long paths.
    enable_language(RC)
    target_sources(xenogears PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xenogears.rc")
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xenogears.rc" APPEND PROPERTY
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xenogears.manifest")
    install(TARGETS xenogears RUNTIME DESTINATION .)
    install(FILES $<TARGET_RUNTIME_DLLS:xenogears> DESTINATION .)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jukebox-font/LICENSE.txt"
        DESTINATION . RENAME LICENSE-Jukebox-font.txt)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/COPYING" DESTINATION . RENAME COPYING.txt)
    if(XG_GLSLANG_LIBS)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glslang/LICENSE.txt"
            DESTINATION . RENAME LICENSE-glslang.txt)
    endif()
    if(XG_DAV1D_LIBS)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dav1d/COPYING"
            DESTINATION . RENAME LICENSE-dav1d.txt)
    endif()
    # SDL3.dll beside the executable in the build directory too, so it runs from there.
    add_custom_command(TARGET xenogears POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:xenogears> $<TARGET_FILE_DIR:xenogears>
        COMMAND_EXPAND_LISTS)
elseif(XG_PIE)
    target_compile_definitions(platform PRIVATE XG_PIE=1)
    if(TARGET xenogears)
        target_link_options(xenogears PRIVATE -pie)
    endif()
else()
    # Non-PIE: fixed addresses (save states hold them), and initialised pointers
    # stay in .data (one section per overlay to reset).
    target_link_options(xenogears PRIVATE -no-pie)
endif()
# Linux: install rules and the .deb (port/cmake/deb.cmake; cpack -G DEB).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID AND NOT XG_SHARED)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/deb.cmake")
endif()

# Link-time optimisation of the platform layer (xg-htn.41.5; on by default:
# frames and audio stay bit-identical, about 2-3% fewer instructions run): its
# objects carry GCC's intermediate code as well and are optimised together
# when the executable is linked. Fat objects, because tools/pc_link.py reads
# them with nm. The game C is not: pc_link links each module with ld -r and
# localises its symbols, which needs ordinary objects. -DXG_LTO=OFF builds
# without it.
option(XG_LTO "link-time optimisation of the platform layer" ON)
if(XG_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT XG_IPO OUTPUT XG_IPO_WHY LANGUAGES C CXX)
    if(XG_IPO)
        set_property(TARGET platform ${XG_GAME_TARGET} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        target_compile_options(platform PRIVATE -ffat-lto-objects)
    else()
        message(STATUS "XG_LTO: no link-time optimisation here (${XG_IPO_WHY})")
    endif()
endif()
# XG_MARCH=x86-64-v3 (or native): the platform layer and the menu built for
# this machine's CPU, for local builds only. Empty (the default) keeps the
# baseline x86-64 that distributed builds need. The game C keeps the default
# target. Floating point still rounds as at the baseline (-ffp-contract=off
# for all port C, at the top).
set(XG_MARCH "" CACHE STRING "-march for the platform layer and the menu (empty: the compiler's default)")
if(XG_MARCH)
    target_compile_options(platform PRIVATE -march=${XG_MARCH})
    target_compile_options(ui PRIVATE -march=${XG_MARCH})
endif()
