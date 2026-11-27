# The Ubuntu package (amd64; Ubuntu 26.04 LTS and later; Debian 14 only if the
# same package passes the install and start test in a clean debian:forky
# container, unproven so far; not Debian 13), made from a Linux Release build
# with CPack:
#   cmake -S port -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release -DXG_VERSION=US -DXG_EXTRAS=ON \
#       -DXG_PACKAGE_VERSION=<release version without the v>
#   cmake --build <build> && (cd <build> && cpack -G DEB)
# gives xenogears-pc_<version>_amd64.deb: the executable in /usr/bin, the
# licences in /usr/share/doc/xenogears-pc, and the release file and the
# executable's state schema in /usr/share/xenogears-pc (tools/check_deb_package.py
# checks it).
# SDL3, glslang and dav1d come from the system's packages, with their licences;
# Depends is worked out by dpkg-shlibdeps from what the executable links. The
# release's Depends must come from building in an Ubuntu 26.04 container (its
# libraries are the oldest of the targets): a host without dpkg records of its
# libraries gives none, so a package built there proves nothing about them. No
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

# A release's file of what it is (port/src/system/release.h: the in-app updater
# reads it, xg-4oq.14): its version and the kind of package that installed it.
# deb by default (this file's package); the PKGBUILD and the ebuild name their
# own (-DXG_INSTALL_KIND=arch, gentoo); the Linux tarball is the .deb's files
# with tarball written in (packaging/linux_release.py). A build of any other
# version (0~dev, a test package's) installs none: it is never offered an update.
set(XG_INSTALL_KIND "deb" CACHE STRING "The package a release's install is (release.txt's kind): deb, arch, gentoo")
set_property(CACHE XG_INSTALL_KIND PROPERTY STRINGS deb arch gentoo)
if(NOT XG_INSTALL_KIND MATCHES "^(deb|arch|gentoo)$")
    message(FATAL_ERROR "XG_INSTALL_KIND '${XG_INSTALL_KIND}' is not deb, arch or gentoo")
endif()
if(XG_PACKAGE_VERSION MATCHES "^20[0-9][0-9]\\.(0[1-9]|1[0-2])\\.(0|[1-9][0-9]*)$")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/release.txt.new" "version=${XG_PACKAGE_VERSION}\nkind=${XG_INSTALL_KIND}\n")
    configure_file("${CMAKE_CURRENT_BINARY_DIR}/release.txt.new" "${CMAKE_CURRENT_BINARY_DIR}/release.txt" COPYONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/release.txt" DESTINATION share/xenogears-pc)
endif()

install(TARGETS xenogears RUNTIME DESTINATION bin)
# Desktop launcher entry and icon (matches SDL_SetAppMetadata app_id "xenogears").
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xenogears.svg"
    DESTINATION share/icons/hicolor/scalable/apps)
if(XG_ENGINE STREQUAL "enhanced")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xenogears-enhanced.desktop"
        DESTINATION share/applications)
else()
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xenogears.desktop"
        DESTINATION share/applications)
endif()
# The executable's state schema (port/CMakeLists.txt XG_STATE_SCHEMA): <prefix>/bin's executable
# reads it from <prefix>/share/xenogears-pc (port/src/system/stateportable.c), one file per engine.
if(XG_STATE_SCHEMA)
    install(FILES "${CMAKE_BINARY_DIR}/${XG_STATE_SCHEMA_FILE}" DESTINATION share/xenogears-pc)
endif()
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
# The update check (XG_UPDATER) opens libcurl.so.4 at run time too; without it System > Updates
# says no check is possible. Recommends, not Depends: apt installs it by default, and a player
# who leaves it out still has the game (carl xg-4oq.14.7).
if(XG_UPDATER)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS libcurl4t64)
endif()
include(CPack)
