# port/src/extras: the extras (the asset browser and the viewers), included by
# port/CMakeLists.txt after the settings menu's library.
#
# - C files join the platform layer: the game code they call (the sandbox's
#   decoders, the file system, LZSS) is then exported from its module by
#   tools/pc_link.py --platform, which only looks at the platform objects.
# - C++ files join the settings menu's library (`ui`): Dear ImGui and the
#   game's look (port/src/ui/skin.h).
# - GLSL shaders in shaders/ are compiled with glslc into SPIR-V word arrays
#   that view.cpp includes (build/.../extras_shaders/<name>.spv.inc).
#
# A viewer is one more .cpp (and .c) file here: nothing in this file changes.

# -DXG_EXTRAS=OFF leaves them out: main.c, vk.c and the menu reach them
# through weak symbols, so the port builds and runs without them.
option(XG_EXTRAS "Build the extras (port/src/extras)" ON)
if(NOT XG_EXTRAS)
    return()
endif()

set(XG_EXTRAS_DIR "${CMAKE_CURRENT_LIST_DIR}")
file(GLOB XG_EXTRAS_C CONFIGURE_DEPENDS "${XG_EXTRAS_DIR}/*.c")
file(GLOB XG_EXTRAS_CXX CONFIGURE_DEPENDS "${XG_EXTRAS_DIR}/*.cpp")
target_sources(platform PRIVATE ${XG_EXTRAS_C})
target_sources(ui PRIVATE ${XG_EXTRAS_CXX})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${XG_EXTRAS_CXX} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter")
endif()

set(XG_EXTRAS_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/extras_shaders")
set(XG_EXTRAS_SHADER_INCS)
foreach(shader view.vert view.frag)
    set(out "${XG_EXTRAS_SHADER_OUT}/${shader}.spv.inc")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${XG_EXTRAS_SHADER_OUT}"
        COMMAND Vulkan::glslc --target-env=vulkan1.3 -O -mfmt=c -o "${out}" "${XG_EXTRAS_DIR}/shaders/${shader}"
        DEPENDS "${XG_EXTRAS_DIR}/shaders/${shader}"
        COMMENT "glslc extras ${shader}"
        VERBATIM
    )
    list(APPEND XG_EXTRAS_SHADER_INCS "${out}")
endforeach()
add_custom_target(extras_shaders DEPENDS ${XG_EXTRAS_SHADER_INCS})
add_dependencies(ui extras_shaders)
target_include_directories(ui PRIVATE "${XG_EXTRAS_SHADER_OUT}")

# The writers on known data (not part of `all`): test/writertest.c.
add_executable(extras_writertest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/writertest.c" "${XG_EXTRAS_DIR}/writers.c")
target_link_libraries(extras_writertest PRIVATE SDL3::SDL3 m)

# Compare the viewers' rates and export default with the actual runtime clock.
# Only the clock/rate functions are retained; no disc or game is needed.
add_executable(extras_ratestest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/ratestest.c"
    "${XG_EXTRAS_DIR}/tables.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/system/hw.c")
target_include_directories(extras_ratestest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_ROOT}/include")
target_compile_definitions(extras_ratestest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_compile_options(extras_ratestest PRIVATE -ffunction-sections -fdata-sections
    $<$<BOOL:${WIN32}>:-fno-asynchronous-unwind-tables;-fno-unwind-tables>)
target_link_options(extras_ratestest PRIVATE -Wl,--gc-sections)
target_link_libraries(extras_ratestest PRIVATE SDL3::SDL3 m)

# Battle effect payload bounds and commands requiring unavailable context.
add_executable(extras_enemyspritestest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/enemyspritestest.c"
    "${XG_EXTRAS_DIR}/sprites_enemy.c")
target_include_directories(extras_enemyspritestest PRIVATE "${XG_EXTRAS_DIR}")

add_executable(extras_battlefxtest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/battlefxtest.c"
    "${XG_EXTRAS_DIR}/battlefx_decode.c")
target_include_directories(extras_battlefxtest PRIVATE "${XG_EXTRAS_DIR}")

# Navigation lifecycle, ownership and rollback without a disc image or GPU.
add_executable(extras_navigationtest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/navigationtest.cpp"
    "${XG_EXTRAS_DIR}/navigation.cpp")
target_include_directories(extras_navigationtest PRIVATE "${XG_EXTRAS_DIR}")
target_compile_features(extras_navigationtest PRIVATE cxx_std_17)

add_executable(extras_field_assetstest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/field_assetstest.c"
    "${XG_EXTRAS_DIR}/field_assets.c")
target_include_directories(extras_field_assetstest PRIVATE "${XG_EXTRAS_DIR}")

add_executable(extras_bookmarktest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/bookmarktest.cpp"
    "${XG_EXTRAS_DIR}/bookmarks.cpp")
target_include_directories(extras_bookmarktest PRIVATE "${XG_EXTRAS_DIR}")
target_compile_features(extras_bookmarktest PRIVATE cxx_std_17)

add_executable(extras_spritecataloguetest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/spritecataloguetest.cpp"
    "${XG_EXTRAS_DIR}/sprites_catalogue.cpp")
target_include_directories(extras_spritecataloguetest PRIVATE "${XG_EXTRAS_DIR}")
target_compile_features(extras_spritecataloguetest PRIVATE cxx_std_17)

add_executable(extras_fieldlocationstest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/fieldlocationstest.cpp"
    "${XG_EXTRAS_DIR}/field_locations.cpp")
target_include_directories(extras_fieldlocationstest PRIVATE "${XG_EXTRAS_DIR}")
target_compile_features(extras_fieldlocationstest PRIVATE cxx_std_17)

add_executable(extras_manifesttest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/manifesttest.cpp"
    "${XG_EXTRAS_DIR}/export_manifest.cpp")
target_include_directories(extras_manifesttest PRIVATE "${XG_EXTRAS_DIR}")
target_compile_features(extras_manifesttest PRIVATE cxx_std_17)

add_executable(extras_observetest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/observetest.c" "${XG_EXTRAS_DIR}/observe.c")
target_include_directories(extras_observetest PRIVATE "${XG_EXTRAS_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_compile_definitions(extras_observetest PRIVATE PLATFORM_PC)

add_executable(extras_field_scriptstest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/field_scriptstest.c" "${XG_EXTRAS_DIR}/gallery_refs.c")
target_include_directories(extras_field_scriptstest PRIVATE "${XG_EXTRAS_DIR}")

# The battle's move selection read from made-up command, level-up and
# script data (battle_moves.c).
add_executable(extras_battlemovestest EXCLUDE_FROM_ALL "${XG_EXTRAS_DIR}/test/battlemovestest.c"
    "${XG_EXTRAS_DIR}/battle_moves.c")
target_include_directories(extras_battlemovestest PRIVATE "${XG_EXTRAS_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
