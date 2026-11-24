# Game code: gnu89 without system headers (it declares what it uses), and the
# PS1 compiler's semantics where C leaves them open: unsigned char, wrapping
# signed arithmetic, type punning, tentative definitions shared across files,
# and explicit zero initialisers in .data, not .bss, as GCC 2.x places them
# (the soft reset clears main's BSS and keeps its data, as the PS1's does).
# One .data and one .bss per object, never a section per symbol (the Android
# NDK's toolchain turns -fdata-sections on): the module link renames .data
# and .bss into the module's own sections, which the overlay resets and save
# states cover.
set(XG_GAME_OPTIONS -std=gnu89 -nostdinc -fno-builtin -funsigned-char -fwrapv -fno-strict-aliasing -fcommon
    -fno-zero-initialized-in-bss -fno-data-sections -fno-function-sections ${XG_PIC_OPTIONS})
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    list(APPEND XG_GAME_OPTIONS -fpermissive -w)
else()
    list(APPEND XG_GAME_OPTIONS -Wno-everything)
    # The state schema finds the types of the game C's extern declarations by name in the
    # image's own debug information (tools/state_schema.py clang_declared), and clang leaves a
    # type out when nothing it describes uses it: one the C reaches only through such a
    # declaration (extern BattlingSideState battlingSideStates[2]). So every type the game C
    # defines goes in; the code is the same.
    if(XG_STATE_SCHEMA)
        list(APPEND XG_GAME_OPTIONS -fno-eliminate-unused-debug-types)
    endif()
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

# port/src/game/<module>/*.c: C versions of the game's hand-written assembly,
# compiled like the game C and linked into that module.
function(xg_game_objects module)
    file(GLOB XG_PORT_GAME_${module} CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/game/${module}/*.c")
    add_library(game_${module} OBJECT ${XG_SOURCES_${module}} ${XG_PORT_GAME_${module}})
    target_include_directories(game_${module} PRIVATE "${XG_GAME_ROOT}/include")
    target_compile_definitions(game_${module} PRIVATE PLATFORM_PC VERSION_${XG_VERSION} _LANGUAGE_C)
    target_compile_options(game_${module} PRIVATE ${XG_GAME_OPTIONS})
endfunction()

function(xg_link_module module)
    if(NOT XG_MODULE_METADATA)
        set(XG_MODULE_METADATA "${XG_GEN_DIR}")
    endif()
    add_library(gen_${module} OBJECT "${XG_GEN_DIR}/${module}.c")
    # -fno-common: the generated aliases (.set name, host + offset) need their host
    # defined in this object. A tentative definition left common has no address there,
    # and the alias stays undefined.
    target_compile_options(gen_${module} PRIVATE -w -fno-data-sections -fno-common ${XG_PIC_OPTIONS})
    set(out "${CMAKE_BINARY_DIR}/modules/${module}.o")
    # Each module's data and BSS in sections of their own: the overlays are
    # reset at each load, main's BSS at the soft reset (port/src/system/memory.c).
    # Position-independent code (XG_PIE) puts initialised data that holds
    # pointers in .data.rel* instead of .data: the module's data all the same.
    set(format)
    set(finish)
    set(plan_input)
    set(plan_output)
    # A checkout without the disc takes what to zero from the module's plan
    # (XG_MODULE_PLAN_DIR: port/link-tables or a test's own); a disc-backed
    # build can write the plan it found (XG_WRITE_TABLES_DIR).
    if(XG_MODULE_PLAN_DIR)
        list(APPEND format --plan "${XG_MODULE_PLAN_DIR}/${module}.json")
        set(plan_input "${XG_MODULE_PLAN_DIR}/${module}.json")
    elseif(XG_WRITE_TABLES_DIR)
        list(APPEND format --write-plan "${XG_WRITE_TABLES_DIR}/srcdata/${module}.json")
        set(plan_output BYPRODUCTS "${XG_WRITE_TABLES_DIR}/srcdata/${module}.json")
    endif()
    if(APPLE)
        set(partial "${out}")
        list(APPEND format --macho-exports "${XG_MODULE_METADATA}/${module}.exports")
    else()
        set(partial "${out}.partial")
        list(APPEND format --objcopy "${XG_OBJCOPY}")
        set(rename --rename-section .data=xgdata_${module} --rename-section .bss=xgbss_${module})
        foreach(rel .data.rel .data.rel.local .data.rel.ro .data.rel.ro.local)
            list(APPEND rename --rename-section ${rel}=xgdata_${module})
        endforeach()
        set(finish COMMAND "${XG_OBJCOPY}" "--keep-global-symbols=${XG_MODULE_METADATA}/${module}.exports"
            "--keep-global-symbols=${XG_GEN_DIR}/${module}.srcsyms" ${rename} "${partial}" "${out}")
    endif()
    # The partial link (ld -r) leaves the game C's own retail bytes (tables and
    # strings as C initialisers) out: zeroed, and filled from the player's disc
    # like the module data (tools/pc_srcdata.py: <module>.src.c has the copies).
    set(srcdata "${XG_GEN_DIR}/${module}.src.c" "${XG_GEN_DIR}/${module}.srcsyms")
    add_custom_command(
        OUTPUT "${out}" ${srcdata}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/modules"
        COMMAND Python3::Interpreter "${XG_ROOT}/tools/pc_srcdata.py" --ver ${XG_VERSION_LOWER}
                --ld "${XG_LD}" ${format} ${module} "${partial}" ${srcdata}
                $<TARGET_OBJECTS:gen_${module}> $<TARGET_OBJECTS:game_${module}>
        ${finish}
        ${plan_output}
        DEPENDS $<TARGET_OBJECTS:game_${module}> $<TARGET_OBJECTS:gen_${module}> "${XG_MODULE_METADATA}/${module}.exports"
                "${XG_ROOT}/tools/pc_srcdata.py" "${XG_ROOT}/tools/pc_macho.py" "${XG_ROOT}/tools/payloadcheck.py" ${plan_input}
        WORKING_DIRECTORY "${XG_ROOT}"
        COMMENT "Linking module ${module}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endfunction()

function(xg_darwin_linker)
    execute_process(COMMAND xcrun --find ld-classic OUTPUT_VARIABLE linker
        OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(XG_LD "${linker}" PARENT_SCOPE)
endfunction()
