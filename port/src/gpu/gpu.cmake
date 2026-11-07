# port/src/gpu: the Vulkan renderer (included by port/CMakeLists.txt).
#
# Only the Vulkan headers are linked: the loader comes from SDL at run time
# (SDL_Vulkan_GetVkGetInstanceProcAddr), the way SDL recommends, so nothing
# has to reach the executable's link line from the `platform` object library.
# The GLSL shaders in shaders/ are compiled with glslc into SPIR-V that the C
# includes as word arrays (build/.../gpu_shaders/<name>.spv.inc).
#
#   cmake --build build/pc/<you> --target gputest   # port/src/gpu/test: primitive tests

find_package(Vulkan REQUIRED COMPONENTS glslc)
target_link_libraries(platform PRIVATE Vulkan::Headers)

set(XG_GPU_DIR "${CMAKE_CURRENT_LIST_DIR}")
add_executable(interpolationtest EXCLUDE_FROM_ALL "${XG_GPU_DIR}/interpolation_core.c"
               "${XG_GPU_DIR}/test/interpolationtest.c")
target_include_directories(interpolationtest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_ROOT}/include")
target_link_libraries(interpolationtest PRIVATE m)
set(XG_GPU_SHADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/gpu_shaders")
set(XG_GPU_SHADER_INCS)
# Pieces the shaders #include (filters.glsl): every shader is rebuilt when one changes.
file(GLOB XG_GPU_SHADER_HEADERS CONFIGURE_DEPENDS "${XG_GPU_DIR}/shaders/*.glsl")

# xg_gpu_shader(<source> <output name> [glslc options...])
function(xg_gpu_shader source name)
    set(out "${XG_GPU_SHADER_OUT}/${name}.spv.inc")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${XG_GPU_SHADER_OUT}"
        COMMAND Vulkan::glslc --target-env=vulkan1.3 -O -mfmt=c ${ARGN} -o "${out}" "${XG_GPU_DIR}/shaders/${source}"
        DEPENDS "${XG_GPU_DIR}/shaders/${source}" ${XG_GPU_SHADER_HEADERS}
        COMMENT "glslc ${name}"
        VERBATIM
    )
    set(XG_GPU_SHADER_INCS ${XG_GPU_SHADER_INCS} "${out}" PARENT_SCOPE)
endfunction()

xg_gpu_shader(draw.vert draw.vert)
xg_gpu_shader(draw.frag draw.frag)
xg_gpu_shader(draw.frag draw_interlock.frag -DINTERLOCK)
xg_gpu_shader(draw.vert draw_wide.vert -DWIDE)
xg_gpu_shader(draw.frag draw_wide.frag -DWIDE)
xg_gpu_shader(draw.frag draw_wide_interlock.frag -DINTERLOCK -DWIDE)
xg_gpu_shader(present.vert present.vert)
xg_gpu_shader(present.frag present.frag)
xg_gpu_shader(present.frag present_picture.frag -DPICTURE_PREPASS)
# The hi-res copy (hires.c): its draws into the copy of VRAM and of the wide
# target, and its transfers.
xg_gpu_shader(hires.vert hires.vert)
xg_gpu_shader(hires.vert hires_wide.vert -DWIDE)
xg_gpu_shader(hires.vert hires_coarse.vert -DCOARSE)
xg_gpu_shader(hires.vert hires_wide_coarse.vert -DWIDE -DCOARSE)
xg_gpu_shader(hires.frag hires.frag)
xg_gpu_shader(hires.frag hires_interlock.frag -DINTERLOCK)
xg_gpu_shader(hires.frag hires_wide.frag -DWIDE)
xg_gpu_shader(hires.frag hires_wide_interlock.frag -DINTERLOCK -DWIDE)
# upscale_large=half: large 3D batches at N/2, each fragment a 2x2 block (hires.c, vk.c drawHiBatch).
xg_gpu_shader(hires.frag hires_coarse.frag -DCOARSE)
xg_gpu_shader(hires.frag hires_coarse_interlock.frag -DINTERLOCK -DCOARSE)
xg_gpu_shader(hires.frag hires_wide_coarse.frag -DWIDE -DCOARSE)
xg_gpu_shader(hires.frag hires_wide_coarse_interlock.frag -DINTERLOCK -DWIDE -DCOARSE)
xg_gpu_shader(hires_util.frag hires_util.frag)
xg_gpu_shader(capture.frag capture.frag)
xg_gpu_shader(draw.frag draw_ordered.frag -DORDERED_LIST)
xg_gpu_shader(draw.frag draw_wide_ordered.frag -DORDERED_LIST -DWIDE)
xg_gpu_shader(hires.frag hires_ordered.frag -DORDERED_LIST)
xg_gpu_shader(hires.frag hires_wide_ordered.frag -DORDERED_LIST -DWIDE)
xg_gpu_shader(ordered.comp ordered.comp)
xg_gpu_shader(draw.vert draw_ordered.vert -DORDERED_LIST)
xg_gpu_shader(draw.vert draw_wide_ordered.vert -DORDERED_LIST -DWIDE)
xg_gpu_shader(hires.vert hires_ordered.vert -DORDERED_LIST)
xg_gpu_shader(hires.vert hires_wide_ordered.vert -DORDERED_LIST -DWIDE)

add_custom_target(gpu_shaders DEPENDS ${XG_GPU_SHADER_INCS})
add_dependencies(platform gpu_shaders)
target_include_directories(platform PRIVATE "${XG_GPU_SHADER_OUT}")

add_executable(texturecachetest EXCLUDE_FROM_ALL "${XG_GPU_DIR}/texture.c" "${XG_GPU_DIR}/vram.c"
               "${XG_GPU_DIR}/test/texturecachetest.c")
target_compile_features(texturecachetest PRIVATE c_std_11)
target_include_directories(texturecachetest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_executable(provenancetest EXCLUDE_FROM_ALL "${XG_GPU_DIR}/texture.c" "${XG_GPU_DIR}/vram.c"
               "${XG_GPU_DIR}/test/provenancetest.c")
target_compile_features(provenancetest PRIVATE c_std_11)
target_include_directories(provenancetest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# The JPEG decoder (jpeg.c: the shader presets' JPEG pictures) on known pictures and damaged data.
add_executable(jpegtest EXCLUDE_FROM_ALL "${XG_GPU_DIR}/jpeg.c" "${XG_GPU_DIR}/test/jpegtest.c")
target_compile_features(jpegtest PRIVATE c_std_11)
target_compile_definitions(jpegtest PRIVATE XG_ROOT="${XG_ROOT}")
target_include_directories(jpegtest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(jpegtest PRIVATE m)

add_executable(texturepacktest EXCLUDE_FROM_ALL "${XG_GPU_DIR}/texture.c" "${XG_GPU_DIR}/vram.c"
               "${XG_GPU_DIR}/texture_pack.c" "${XG_GPU_DIR}/texture_beetle.c" "${XG_GPU_DIR}/texture_dump.c" "${XG_GPU_DIR}/pngfast.c"
               "${XG_GPU_DIR}/test/texturepacktest.c"
               "${XG_GPU_DIR}/../system/settings.c" "${XG_GPU_DIR}/../system/engine.c"
               "${XG_GPU_DIR}/../system/cputime.c")
target_compile_features(texturepacktest PRIVATE c_std_11)
target_compile_definitions(texturepacktest PRIVATE VERSION_${XG_VERSION})
target_include_directories(texturepacktest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(texturepacktest PRIVATE SDL3::SDL3)

add_executable(portraitpacktest EXCLUDE_FROM_ALL "${XG_GPU_DIR}/texture.c" "${XG_GPU_DIR}/vram.c"
               "${XG_GPU_DIR}/texture_pack.c" "${XG_GPU_DIR}/texture_beetle.c" "${XG_GPU_DIR}/texture_dump.c" "${XG_GPU_DIR}/pngfast.c"
               "${XG_GPU_DIR}/portrait_pack.c" "${XG_GPU_DIR}/test/portraitpacktest.c"
               "${XG_GPU_DIR}/../system/settings.c" "${XG_GPU_DIR}/../system/engine.c"
               "${XG_GPU_DIR}/../system/cputime.c")
target_compile_features(portraitpacktest PRIVATE c_std_11)
target_compile_definitions(portraitpacktest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_include_directories(portraitpacktest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_ROOT}/include")
target_link_libraries(portraitpacktest PRIVATE SDL3::SDL3)

# Beetle PSX HW packs without a GPU: upload names, ownership, palettes, the folder's rules,
# samples (one image, composites), precedence against the texture pack, the A/B.
add_executable(beetlepacktest EXCLUDE_FROM_ALL "${XG_GPU_DIR}/texture.c" "${XG_GPU_DIR}/vram.c"
               "${XG_GPU_DIR}/texture_pack.c" "${XG_GPU_DIR}/texture_beetle.c" "${XG_GPU_DIR}/texture_dump.c"
               "${XG_GPU_DIR}/pngfast.c" "${XG_GPU_DIR}/test/beetlepacktest.c"
               "${XG_GPU_DIR}/../system/settings.c" "${XG_GPU_DIR}/../system/engine.c"
               "${XG_GPU_DIR}/../system/cputime.c")
target_compile_features(beetlepacktest PRIVATE c_std_11)
target_compile_definitions(beetlepacktest PRIVATE VERSION_${XG_VERSION})
target_include_directories(beetlepacktest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(beetlepacktest PRIVATE SDL3::SDL3)

# Cel HUD libraries without a GPU: sprites.txt parsing, budget, outcome counts.
add_executable(celpacktest EXCLUDE_FROM_ALL "${XG_GPU_DIR}/texture.c" "${XG_GPU_DIR}/vram.c"
               "${XG_GPU_DIR}/texture_pack.c" "${XG_GPU_DIR}/texture_beetle.c" "${XG_GPU_DIR}/texture_dump.c" "${XG_GPU_DIR}/pngfast.c"
               "${XG_GPU_DIR}/texture_cel.c" "${XG_GPU_DIR}/test/celpacktest.c"
               "${XG_GPU_DIR}/../system/settings.c" "${XG_GPU_DIR}/../system/engine.c"
               "${XG_GPU_DIR}/../system/cputime.c")
target_compile_features(celpacktest PRIVATE c_std_11)
target_compile_definitions(celpacktest PRIVATE VERSION_${XG_VERSION})
target_include_directories(celpacktest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(celpacktest PRIVATE SDL3::SDL3)

add_executable(glyphtest EXCLUDE_FROM_ALL "${XG_GPU_DIR}/texture.c" "${XG_GPU_DIR}/vram.c"
               "${XG_GPU_DIR}/texture_pack.c" "${XG_GPU_DIR}/texture_beetle.c" "${XG_GPU_DIR}/texture_dump.c" "${XG_GPU_DIR}/texture_glyph.c"
               "${XG_GPU_DIR}/pngfast.c"
               "${XG_GPU_DIR}/test/glyphtest.c" "${XG_GPU_DIR}/../system/settings.c" "${XG_GPU_DIR}/../system/engine.c"
               "${XG_GPU_DIR}/../system/cputime.c")
target_compile_features(glyphtest PRIVATE c_std_11)
target_compile_definitions(glyphtest PRIVATE PLATFORM_PC VERSION_${XG_VERSION})
target_include_directories(glyphtest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_ROOT}/include")
target_link_libraries(glyphtest PRIVATE SDL3::SDL3)

# Packets are read and written through the game's punned views (P_TAG, OTag,
# GP0 words), as the game code itself is compiled: no strict aliasing.
file(GLOB XG_GPU_SOURCES CONFIGURE_DEPENDS "${XG_GPU_DIR}/*.c")
set_source_files_properties(${XG_GPU_SOURCES} PROPERTIES COMPILE_OPTIONS -fno-strict-aliasing)

# Test programs (not part of `all`): the GPU layer with a minimal clock.
#   gputest      primitives with known results (test/gputest.c)
#   gpucase      a GP0 operation list (the black-box measurements' workloads)
#   gpureplay    a LIBGPU call trace recorded in PCSX-Redux, compared with its VRAM
#   presenttest  every display mode at several window sizes through the
#                present pass (test/presenttest.c)
#   slangtest    shader presets on the present pass (test/slangtest.c)
#   moviepacktest  a pack's movie pictures on the present pass (test/moviepacktest.c)
#   presentfiltertest  the present pass's filters on pictures of known shape
#                (test/presentfiltertest.c)
foreach(tool celgputest orderedgputest cpueffecttest highrefreshtest interpolationgputest gputest gpucase gpureplay presenttest presentfiltertest verticaltest distortiontest transitiontest geometrytest texturetest texturefiltertest textureedgetest texturepackgputest portraitgputest glyphgputest promptgputest slangtest moviepacktest autoresolutiontest)
    if(NOT XG_GPU_SOURCES OR NOT EXISTS "${XG_GPU_DIR}/test/${tool}.c")
        continue()
    endif()
    # present.c keeps its display options in settings.ini (port/src/system/settings.c).
    set(clock "${XG_GPU_DIR}/test/testclock.c")
    if(tool STREQUAL "highrefreshtest")
        set(clock "${XG_GPU_DIR}/../system/hw.c"
            "${XG_GPU_DIR}/../spu/core.c" "${XG_GPU_DIR}/../spu/voice.c"
            "${XG_GPU_DIR}/../spu/reverb.c" "${XG_GPU_DIR}/../spu/tables.c"
            "${XG_GPU_DIR}/../spu/cdin.c")
    endif()
    add_executable(${tool} EXCLUDE_FROM_ALL ${XG_GPU_SOURCES} "${XG_GPU_DIR}/test/${tool}.c" ${clock}
                   "${XG_GPU_DIR}/../system/settings.c" "${XG_GPU_DIR}/../system/engine.c" "${XG_GPU_DIR}/../system/speedup.c"
                   "${XG_GPU_DIR}/../gte/precision.c" "${XG_GPU_DIR}/../system/cputime.c")
    add_dependencies(${tool} gpu_shaders)
    target_compile_features(${tool} PRIVATE c_std_11)
    target_include_directories(${tool} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${XG_ROOT}/include" "${XG_GPU_SHADER_OUT}")
    target_compile_definitions(${tool} PRIVATE PLATFORM_PC VERSION_${XG_VERSION} XG_ROOT="${XG_ROOT}")
    target_compile_options(${tool} PRIVATE -fno-strict-aliasing)
    target_link_libraries(${tool} PRIVATE SDL3::SDL3 Vulkan::Headers m)
    if(XG_GLSLANG_LIBS)
        # shader presets (shaderchain.c) with the compiler, as in the game
        target_compile_definitions(${tool} PRIVATE XG_HAVE_GLSLANG=1)
        target_link_libraries(${tool} PRIVATE ${XG_GLSLANG_LIBS})
    endif()
    if(XG_DAV1D_LIBS)
        # AV1 movie videos (av1video.c) with the decoder, as in the game
        target_compile_definitions(${tool} PRIVATE XG_HAVE_DAV1D=1)
        target_include_directories(${tool} PRIVATE "${XG_DAV1D_INCLUDE}")
        target_link_libraries(${tool} PRIVATE ${XG_DAV1D_LIBS})
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${tool} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endforeach()
target_compile_options(highrefreshtest PRIVATE -ffunction-sections -fdata-sections)
target_link_options(highrefreshtest PRIVATE -Wl,--gc-sections)
if(MINGW)
    target_compile_options(highrefreshtest PRIVATE -fno-asynchronous-unwind-tables -fno-unwind-tables)
endif()
target_sources(distortiontest PRIVATE "${XG_ROOT}/src/field/f_800A47D4.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/main/retailheap.c")

# Compile the actual transition bodies without the unrelated movie/loader
# functions in the same overlay translation unit (COFF keeps their refptrs).
set(XG_TRANSITION_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/transition_fixture.c")
add_custom_command(OUTPUT "${XG_TRANSITION_FIXTURE}"
    COMMAND Python3::Interpreter "${XG_ROOT}/tools/transition_fixture.py"
            "${XG_ROOT}/src/field/f_800A55B8.c" "${XG_TRANSITION_FIXTURE}"
    DEPENDS "${XG_ROOT}/tools/transition_fixture.py" "${XG_ROOT}/tools/splice.py"
            "${XG_ROOT}/src/field/f_800A55B8.c"
    VERBATIM)
target_sources(transitiontest PRIVATE "${XG_TRANSITION_FIXTURE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/main/retailheap.c"
               "${XG_ROOT}/port/src/gte/gte.c" "${XG_ROOT}/port/src/gte/libgte.c"
               "${XG_ROOT}/port/src/game/main/trig.c")
target_compile_options(transitiontest PRIVATE -ffunction-sections -fdata-sections)
target_link_options(transitiontest PRIVATE -Wl,--gc-sections)
if(MINGW)
    # COFF unwind records retain otherwise unused LIBGTE functions.
    target_compile_options(transitiontest PRIVATE -fno-asynchronous-unwind-tables -fno-unwind-tables)
endif()

# Exercise the actual captured-background body in verticaltest; discard its
# unrelated projection entry point, whose game math is not needed here.
target_compile_options(verticaltest PRIVATE -ffunction-sections -fdata-sections)
target_link_options(verticaltest PRIVATE -Wl,--gc-sections)
