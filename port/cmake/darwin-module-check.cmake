if(NOT APPLE OR NOT XG_VERSION STREQUAL "US" OR NOT XG_ENGINE STREQUAL "original")
    message(FATAL_ERROR "The checked module inputs are for US Original on arm64 macOS")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(XG_CHECK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/system/test/macos_modules")
set(XG_MODULE_METADATA "${XG_CHECK_DIR}/us-original")
set(XG_MODULE_PLAN_DIR "${XG_MODULE_METADATA}")
set(XG_VERSION_LOWER us)
set(XG_GEN_DIR "${CMAKE_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${XG_GEN_DIR}")
# Keep compiler-generated jump-table bytes out of the source initializer plan.
# -O0 also keeps private objects visible for the native symbol checks.
list(APPEND XG_GAME_OPTIONS -O0 -fno-jump-tables)
foreach(module field battle)
    xg_game_objects(${module})
endforeach()
add_custom_target(darwin_module_inputs DEPENDS game_field game_battle)
if(CMAKE_CROSSCOMPILING)
    return() # Maintainer preparation only needs the input Mach-O objects.
endif()
xg_darwin_linker()
foreach(module field battle)
    configure_file("${XG_MODULE_METADATA}/${module}.c" "${XG_GEN_DIR}/${module}.c" COPYONLY)
    xg_link_module(${module})
    list(APPEND objects "${CMAKE_BINARY_DIR}/modules/${module}.o")
    list(APPEND copies "${XG_GEN_DIR}/${module}.src.c")
endforeach()
enable_language(ASM)
add_custom_command(OUTPUT "${XG_GEN_DIR}/imports.s"
    COMMAND Python3::Interpreter "${XG_CHECK_DIR}/game_check.py" imports "${CMAKE_BINARY_DIR}"
    DEPENDS ${objects} "${XG_CHECK_DIR}/game_check.py" VERBATIM)
add_executable(darwin_module_check "${XG_GEN_DIR}/imports.s" "${XG_CHECK_DIR}/game_reset.c" src/system/memory.c
    "${XG_MODULE_METADATA}/bounds.c" ${objects} ${copies})
