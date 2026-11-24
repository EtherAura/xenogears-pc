# The Debian amd64 builder for the release's Windows executables: an image
# recipe only.
#
# It installs Debian's own MinGW-w64 cross compiler (GCC 16, POSIX threads,
# the msvcrt runtime: what the Windows release was built with before it had a
# builder of its own) and the tools the Windows build runs on the build
# machine: CMake, Ninja, Meson and NASM for the libraries, glslc for the
# shaders, the native GCC for the module link's layout probes, Python with
# PyYAML, the Vulkan headers, and LLVM's llvm-dwarfdump, which reads the
# executables' debug information for their state schemas. Nothing here
# configures or builds: packaging/build-windows-zip.sh runs
# tools/build_windows.sh, the payload
# audits and the strip inside this image's root filesystem, under bubblewrap,
# with no network, its work folder at /work/w and the dependency cache at
# /work/cache (so no folder of the machine that runs it reaches the build).
# build-release.sh --fetch-only builds the image from an empty folder and
# keeps its root filesystem in the cache:
#
#   docker build --platform linux/amd64 -t xenogears-pc-mingw -f <this file> <empty folder>
#
# Ubuntu's MinGW (GCC 13) cannot build the port (port/src/gpu/wide.c uses
# #embed), so this is Debian's, not the Linux builder's Ubuntu.
#
# The same compiler for everyone. The base image is named by its digest and
# every package but one comes from one snapshot of Debian's archive (apt's own
# --snapshot, served by snapshot.debian.org for the sources the base image
# lists), so an image built from this recipe next year holds the compilers,
# linkers and libraries it holds now. The exception is ca-certificates,
# installed first from the current archive: the snapshot service speaks only
# TLS and the base image trusts no authority yet. A release's link tables for
# Windows are made with this image's compilers and fit their objects
# (port/CMakeLists.txt XG_LINK_FROM): tables made before a change to the
# digest, the snapshot or the package list do not fit the builder after it.
#
# Network is used only while the image is built (apt from Debian's own
# archive, as the base image lists it; no other source is added and no suite
# replaced). The build that runs in it downloads, installs, starts and uploads
# nothing.

FROM --platform=linux/amd64 debian:forky@sha256:15f483ba923e165dde04ac535be6f479a62008ae5fefb42fed21896288ce447a

# gcc-mingw-w64-x86-64-posix, g++-mingw-w64-x86-64-posix: the cross compilers
# (the posix variant only, so x86_64-w64-mingw32-gcc is it); their binutils and
# the mingw-w64 headers and import libraries; gcc, g++ and libc6-dev for the
# build machine; cmake, ninja-build, meson, nasm, xz-utils (dav1d's tarball);
# glslc; python3 and python3-yaml; libvulkan-dev for the Vulkan headers; llvm
# (llvm-dwarfdump: tools/state_schema.py, port/CMakeLists.txt XG_STATE_SCHEMA).
RUN set -eu; \
    grep -Eq '^Components:.*[[:space:]]main([[:space:]]|$)' /etc/apt/sources.list.d/debian.sources; \
    apt-get update; \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates; \
    rm -rf /var/lib/apt/lists/*; \
    apt-get update --snapshot=20261120T000000Z -o APT::Update::Error-Mode=any; \
    DEBIAN_FRONTEND=noninteractive apt-get install --snapshot=20261120T000000Z -y --no-install-recommends \
        gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev \
        gcc g++ libc6-dev cmake ninja-build meson nasm xz-utils glslc python3 python3-yaml libvulkan-dev llvm; \
    rm -rf /var/lib/apt/lists/*; \
    useradd --no-log-init --create-home --uid 10001 --user-group xgbuild; \
    mkdir -p /work

USER 10001:10001
WORKDIR /tmp
