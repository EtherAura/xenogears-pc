# The Debian/Ubuntu package (amd64; Ubuntu 25.04+ and Debian 13, which package
# SDL3), made from a Linux Release build with CPack:
#   cmake -S port -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release -DXG_VERSION=US -DXG_EXTRAS=ON \
#       -DXG_PACKAGE_VERSION=<release version without the v>
#   cmake --build <build> && (cd <build> && cpack -G DEB)
# gives xenogears-pc_<version>_amd64.deb: the executable in /usr/bin and the
# licences in /usr/share/doc/xenogears-pc (tools/check_deb_package.py checks it).
# SDL3, glslang and dav1d come from the system's packages, with their licences;
# Depends is worked out by dpkg-shlibdeps from what the executable links. No
# game data: the port reads the player's own disc.

# The version comes from the release (its tag without the v), never written
# here; local builds are 0~dev, which sorts before every release (empty too:
# an existing build's cache may hold the variable empty).
set(XG_PACKAGE_VERSION "0~dev" CACHE STRING "Package version (the release's tag without its v)")
if(XG_PACKAGE_VERSION STREQUAL "")
    set_property(CACHE XG_PACKAGE_VERSION PROPERTY VALUE "0~dev")
    set(XG_PACKAGE_VERSION "0~dev")
endif()
if(NOT XG_PACKAGE_VERSION MATCHES "^[0-9][A-Za-z0-9.+~]*(-[A-Za-z0-9.+~]+)?$")
    message(FATAL_ERROR "XG_PACKAGE_VERSION '${XG_PACKAGE_VERSION}' is not a Debian version "
        "(digits first, then letters, digits and . + ~; a release tag's v left out)")
endif()

install(TARGETS xenogears RUNTIME DESTINATION bin)
set(XG_DOC_DIR share/doc/xenogears-pc)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/COPYING" DESTINATION ${XG_DOC_DIR})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jukebox-font/LICENSE.txt"
    DESTINATION ${XG_DOC_DIR} RENAME LICENSE-Jukebox-font.txt)
install(FILES "${XG_IMGUI_DIR}/LICENSE.txt" DESTINATION ${XG_DOC_DIR} RENAME LICENSE-imgui.txt)
# As in the Windows install: their licences beside the port's whenever linked
# (a distribution's glslang may be static libraries, compiled into the executable).
if(XG_GLSLANG_LIBS)
    install(FILES "${XG_GLSLANG_LICENSE}" DESTINATION ${XG_DOC_DIR} RENAME LICENSE-glslang.txt)
endif()
if(XG_DAV1D_LIBS)
    install(FILES "${XG_DAV1D_LICENSE}" DESTINATION ${XG_DOC_DIR} RENAME LICENSE-dav1d.txt)
endif()

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME xenogears-pc)
set(CPACK_PACKAGE_VERSION "${XG_PACKAGE_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Xenogears PC port (reads the player's own disc)")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_STRIP_FILES ON)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(CPACK_DEBIAN_PACKAGE_VERSION "${XG_PACKAGE_VERSION}")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
endif() # elsewhere CPack asks dpkg --print-architecture
if(CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    set(CPACK_DEBIAN_FILE_NAME "xenogears-pc_${XG_PACKAGE_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
else()
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "EtherAura")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/EtherAura/xenogears-pc")
set(CPACK_DEBIAN_PACKAGE_SECTION games)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# SDL3 opens the Vulkan loader at run time (dlopen), which shlibdeps cannot see.
set(CPACK_DEBIAN_PACKAGE_DEPENDS libvulkan1)
include(CPack)
