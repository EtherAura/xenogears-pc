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

# Floating point rounds as written, on every target and CPU: no FMA
# contraction, also where the baseline has FMA (aarch64) or XG_MARCH enables
# it. The enhanced rasteriser's CPU tests compare its batched evaluation
# with the original order byte for byte (gp0.c hiSeal, test/seamreusetest.c),
# and replayed pictures must equal live ones. (Not for windres: C/C++ only.)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=off>")
endif()

set(XG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

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

set(XG_SOURCES_CMAKE "${XG_ROOT}/build/pc/sources.cmake")
if(NOT EXISTS "${XG_SOURCES_CMAKE}")
    message(FATAL_ERROR "${XG_SOURCES_CMAKE} missing: run python tools/pc_sources.py")
endif()

include("${XG_SOURCES_CMAKE}")
set(XG_MODULES ${XG_MODULES_${XG_VERSION}})
foreach(module IN LISTS XG_MODULES)
    set(XG_SOURCES_${module} ${XG_SOURCES_${XG_VERSION}_${module}})
endforeach()

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
    src/system/pad.c src/system/mouse.c src/system/rumble.c src/system/settings.c)
target_include_directories(pad_profiletest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_GAME_ROOT}/include")
target_compile_definitions(pad_profiletest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_link_libraries(pad_profiletest PRIVATE SDL3::SDL3)
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
add_executable(rewindpacktest EXCLUDE_FROM_ALL src/system/test/rewindpacktest.c src/system/statecodec.c)
add_executable(seamreusetest EXCLUDE_FROM_ALL src/gpu/test/seamreusetest.c)
add_executable(spansteptest EXCLUDE_FROM_ALL src/gpu/test/spansteptest.c)
add_executable(battlepacingtest EXCLUDE_FROM_ALL src/system/test/battlepacingtest.c
    src/system/libetc.c src/system/speedup.c src/system/settings.c)
# hw.c's host presentation budget on the real clock (includes hw.c whole).
add_executable(hostpolltest EXCLUDE_FROM_ALL src/system/test/hostpolltest.c)
# The game's identity in a save's free tail (gameid.c).
add_executable(gameidtest EXCLUDE_FROM_ALL src/system/test/gameidtest.c src/system/gameid.c)
foreach(tool rewindpacktest seamreusetest spansteptest battlepacingtest hostpolltest gameidtest)
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

# Game code: gnu89 without system headers (it declares what it uses), and the
# PS1 compiler's semantics where C leaves them open: unsigned char, wrapping
# signed arithmetic, type punning, tentative definitions shared across files,
# and explicit zero initialisers in .data, not .bss, as GCC 2.x places them
# (the soft reset clears main's BSS and keeps its data, as the PS1's does).
set(XG_GAME_OPTIONS -std=gnu89 -nostdinc -fno-builtin -funsigned-char -fwrapv -fno-strict-aliasing -fcommon
    -fno-zero-initialized-in-bss -fno-pie)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    list(APPEND XG_GAME_OPTIONS -fpermissive -w)
else()
    list(APPEND XG_GAME_OPTIONS -Wno-everything)
endif()

# Windows (MinGW, port/cmake/mingw-w64-x86_64.cmake): bitfields laid out as
# GCC does elsewhere (as tools/pc_link.py's probes see them), not as MSVC
# does; data addressed directly, not through .refptr indirection words
# (nothing in game code comes from a DLL).
set(XG_WIN32_LAYOUT_OPTIONS)
if(WIN32)
    set(XG_WIN32_LAYOUT_OPTIONS -mno-ms-bitfields)
    list(APPEND XG_GAME_OPTIONS ${XG_WIN32_LAYOUT_OPTIONS} -mcmodel=small)
    # kernel32's EnterCriticalSection is linked in by the C runtime: the
    # PsyQ one takes another name (port/src/system/libapi.c).
    list(APPEND XG_GAME_OPTIONS -DEnterCriticalSection=psxEnterCriticalSection)
endif()

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
xg_gameplay_test(gameplay_bugfixtest SOURCES src/system/test/bugfixtest.c
    GAME gameplay_bugfix_code gameplay_bugfix_save_code gameplay_bugfix_battle_code gameplay_bugfix_equip_code
    gameplay_bugfix_shop_code)

# Dialogue portraits.
xg_gameplay_code(gameplay_portrait_code "${XG_GAME_ROOT}/src/field/f_8009BB0C.c" "${XG_GAME_ROOT}/src/field/f_8006FDEC.c"
    "${XG_GAME_ROOT}/src/field/f_8007DCF8.c")
xg_gameplay_test(gameplay_portraittest SOURCES src/system/test/portraittest.c src/system/portraitfix.c
    src/game/main/retailheap.c GAME gameplay_portrait_code HOST src/gpu/prim.c)
target_compile_options(gameplay_portraittest PRIVATE -ffunction-sections -fdata-sections)

# port/src/game/<module>/*.c: C versions of the game's hand-written assembly,
# compiled like the game C and linked into that module.
foreach(module IN LISTS XG_MODULES)
    file(GLOB XG_PORT_GAME_${module} CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/game/${module}/*.c")
    add_library(game_${module} OBJECT ${XG_SOURCES_${module}} ${XG_PORT_GAME_${module}})
    target_include_directories(game_${module} PRIVATE "${XG_GAME_ROOT}/include")
    target_compile_definitions(game_${module} PRIVATE PLATFORM_PC VERSION_${XG_VERSION} _LANGUAGE_C)
    target_compile_options(game_${module} PRIVATE ${XG_GAME_OPTIONS})
endforeach()

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
target_compile_definitions(platform PRIVATE PLATFORM_PC VERSION_${XG_VERSION} XG_ROOT="${XG_ROOT}"
    XG_ENGINE_ENHANCED=${XG_ENGINE_ENHANCED})
# -fno-strict-aliasing: platform code reads the game's packets and buffers
# through the same punned views the game uses.
target_compile_options(platform PRIVATE -fno-pie -fno-strict-aliasing ${XG_WIN32_LAYOUT_OPTIONS})
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
# The About section's build line, made again at each build (port/cmake/buildinfo.cmake).
add_custom_target(ui_buildinfo
    COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_BINARY_DIR}/gen/ui_buildinfo.h -DROOT=${XG_ROOT}
            -DVERSION=${XG_VERSION} "-DTYPE=${CMAKE_BUILD_TYPE}${XG_BUILD_TYPE_SUFFIX}" -DIMGUI_LICENSE=${XG_IMGUI_DIR}/LICENSE.txt
            -DSDL_LICENSE=${SDL3_DIR}/../../../share/licenses/SDL3/LICENSE.txt
            "-DGLSLANG_LICENSE=${XG_GLSLANG_LICENSE}" "-DDAV1D_LICENSE=${XG_DAV1D_LICENSE}"
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
find_program(XG_LD NAMES ld REQUIRED)
find_program(XG_OBJCOPY NAMES objcopy REQUIRED)
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
add_custom_command(
    OUTPUT ${XG_GEN_OUTPUTS}
    COMMAND Python3::Interpreter "${XG_ROOT}/tools/pc_link.py" --ver ${XG_VERSION_LOWER}
            --cc "${XG_PROBE_CC}" --game-root "${XG_GAME_ROOT}" "${XG_GEN_DIR}" ${XG_ALL_OBJECTS}
            --platform $<TARGET_OBJECTS:platform>
    DEPENDS ${XG_ALL_OBJECTS} $<TARGET_OBJECTS:platform> "${XG_ROOT}/tools/pc_link.py" ${XG_PCLINK_INPUTS}
    WORKING_DIRECTORY "${XG_ROOT}"
    COMMENT "Generating stubs, data and export lists (tools/pc_link.py)"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

set(XG_MODULE_OBJECTS)
set(XG_SRCDATA_SOURCES)
foreach(module IN LISTS XG_MODULES)
    add_library(gen_${module} OBJECT "${XG_GEN_DIR}/${module}.c")
    target_compile_options(gen_${module} PRIVATE -w -fno-pie)
    set(out "${CMAKE_BINARY_DIR}/modules/${module}.o")
    # Each module's data and BSS in sections of their own: the overlays are
    # reset at each load, main's BSS at the soft reset (port/src/system/memory.c).
    set(rename --rename-section .data=xgdata_${module} --rename-section .bss=xgbss_${module})
    # The partial link (ld -r) leaves the game C's own retail bytes (tables and
    # strings as C initialisers) out: zeroed, and filled from the player's disc
    # like the module data (tools/pc_srcdata.py: <module>.src.c has the copies).
    set(srcdata "${XG_GEN_DIR}/${module}.src.c" "${XG_GEN_DIR}/${module}.srcsyms")
    add_custom_command(
        OUTPUT "${out}" ${srcdata}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/modules"
        COMMAND Python3::Interpreter "${XG_ROOT}/tools/pc_srcdata.py" --ver ${XG_VERSION_LOWER}
                --ld "${XG_LD}" --objcopy "${XG_OBJCOPY}" ${module} "${out}.partial" ${srcdata}
                $<TARGET_OBJECTS:gen_${module}> $<TARGET_OBJECTS:game_${module}>
        COMMAND "${XG_OBJCOPY}" "--keep-global-symbols=${XG_GEN_DIR}/${module}.exports"
                "--keep-global-symbols=${XG_GEN_DIR}/${module}.srcsyms" ${rename} "${out}.partial" "${out}"
        DEPENDS $<TARGET_OBJECTS:game_${module}> $<TARGET_OBJECTS:gen_${module}> "${XG_GEN_DIR}/${module}.exports"
                "${XG_ROOT}/tools/pc_srcdata.py" "${XG_ROOT}/tools/payloadcheck.py"
        WORKING_DIRECTORY "${XG_ROOT}"
        COMMENT "Linking module ${module}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    list(APPEND XG_MODULE_OBJECTS "${out}")
    list(APPEND XG_SRCDATA_SOURCES "${XG_GEN_DIR}/${module}.src.c")
endforeach()
set_source_files_properties(${XG_MODULE_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_library(game_generated OBJECT "${XG_GEN_DIR}/stubs.c" "${XG_GEN_DIR}/modules.c" ${XG_SRCDATA_SOURCES})
target_compile_options(game_generated PRIVATE -w -fno-pie)

# Platform layer and entry point.
add_executable(xenogears
    $<TARGET_OBJECTS:platform>
    $<TARGET_OBJECTS:ui>
    ${XG_MODULE_OBJECTS}
    $<TARGET_OBJECTS:game_generated>
)
set_target_properties(xenogears PROPERTIES LINKER_LANGUAGE CXX)
if(XG_ENGINE STREQUAL "enhanced")
    set_target_properties(xenogears PROPERTIES OUTPUT_NAME xenogears-enhanced)
endif()
target_link_libraries(xenogears PRIVATE SDL3::SDL3 m ${XG_GLSLANG_LIBS} ${XG_DAV1D_LIBS})
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
else()
    # Non-PIE, so initialised pointers stay in .data (one section per overlay to reset).
    target_link_options(xenogears PRIVATE -no-pie)
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
        set_property(TARGET platform xenogears PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
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
