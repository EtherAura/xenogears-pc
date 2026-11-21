# The Ubuntu 26.04 amd64 builder for the release .deb: an image recipe only.
#
# It installs Ubuntu's own build tools and the port's build dependencies, then
# runs packaging/deb_ubuntu.py, unchanged, from the mounted source tree. That
# script and the helpers beside it (deb_build.py, deb_assets.py) do every step:
# the checks, the Release configure, the build, cpack, the strict Depends check,
# the .sha256, the SBOM and the staging. Nothing here configures, builds or
# packages by itself.
#
# Build the image (from any empty folder: the recipe copies nothing into it):
#
#   docker build --platform linux/amd64 -t xenogears-pc-deb -f <this file> <empty folder>
#
# Run it, one new empty build folder per run:
#
#   docker run --rm --platform linux/amd64 --network none \
#       --mount type=bind,src=<public port tree>,dst=/work/source,readonly \
#       --mount type=bind,src=<new empty folder>,dst=/work/build \
#       --mount type=bind,src=<output folder>,dst=/work/out \
#       xenogears-pc-deb --tag v<YYYY>.<MM>.<N>
#
# /work/source: the already generated public port tree (the one holding
# packaging/ and port/), mounted read-only. It must already hold
# build/pc/sources.cmake (tools/pc_sources.py makes it from a disc split,
# outside this image); deb_ubuntu.py refuses to start without it, and nothing
# here generates it or reads a disc. Mount no disc, game data, home folder,
# credentials or private documents, and not the Docker socket.
# /work/build: a new, empty folder of its own (deb_ubuntu.py refuses any other).
# /work/out: where the .deb, its .sha256 and its .cdx.json are staged; it must
# not already hold those three names. Neither may be inside the other or the
# source tree.
#
# The helper runs as the unprivileged user xgbuild, uid 10001, gid 10001, not
# root. The build and output mounts must be writable by that uid (on the host,
# folders owned by 10001:10001, for instance); the source mount needs only to
# be readable by it. No other uid (docker run --user) is claimed to work, and
# the container needs no privilege.
#
# Network is used only while the image is built (apt from Ubuntu's own
# archive, main and universe, as the base image lists them; no other source is
# added and no suite replaced). A run downloads, installs, starts and uploads
# nothing, which --network none above enforces.
#
# The same compiler for everyone. The base image is named by its digest and
# every package but one comes from one snapshot of Ubuntu's archive (apt's own
# --snapshot, served by Ubuntu's snapshot service for the sources the base
# image lists), so an image built from this recipe next year holds the
# compilers, linker and libraries it holds now. The exception is
# ca-certificates, installed first from the current archive: the snapshot
# service speaks only TLS and the base image trusts no authority yet. A
# release's link tables are made with this image's compilers and fit their
# objects (port/CMakeLists.txt XG_LINK_FROM): tables made before a change to
# the digest, the snapshot or the package list do not fit the builder after it.
#
# Limits: this recipe is not tested by building or running it; that the image
# builds, that its dpkg-shlibdeps gives the release's Depends, and that the
# package installs and starts on a clean Ubuntu 26.04 or Debian are not shown
# by tools/check_deb_container.py, which checks only this text and the run
# command's argument passing.

FROM --platform=linux/amd64 ubuntu:26.04@sha256:d66b366dac61b575bee2aef07e9c26e8070096fbe84a3d4d3eb0da069b5d76db

# Main: build-essential (gcc, g++, make, libc6-dev, dpkg-dev), cmake (with
# cpack), dpkg-dev (dpkg-shlibdeps), binutils (ld, objcopy), file (used by
# CPack's shlibdeps), python3, libvulkan-dev. Universe: ninja-build,
# libsdl3-dev, glslc, glslang-dev, glslang-tools, spirv-tools-dev, libdav1d-dev.
# PyYAML supports the combined release build's audits. NSIS provides its
# cached makensis compiler for the Windows installer.
# glslang-dev's CMake targets require the glslang executable from glslang-tools.
# The base image's sources are checked, never edited: without main and
# universe the image build fails.
RUN set -eu; \
    grep -Eq '^Components:.*[[:space:]]main([[:space:]]|$)' /etc/apt/sources.list.d/ubuntu.sources; \
    grep -Eq '^Components:.*[[:space:]]universe([[:space:]]|$)' /etc/apt/sources.list.d/ubuntu.sources; \
    apt-get update; \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates; \
    rm -rf /var/lib/apt/lists/*; \
    apt-get update --snapshot=20261120T000000Z -o APT::Update::Error-Mode=any; \
    DEBIAN_FRONTEND=noninteractive apt-get install --snapshot=20261120T000000Z -y --no-install-recommends \
        build-essential cmake ninja-build dpkg-dev binutils file python3 python3-yaml \
        libsdl3-dev libvulkan-dev glslc glslang-dev glslang-tools spirv-tools-dev libdav1d-dev nsis; \
    rm -rf /var/lib/apt/lists/*; \
    useradd --no-log-init --create-home --uid 10001 --user-group xgbuild; \
    mkdir -p /work/source /work/build /work/out

USER 10001:10001
WORKDIR /work
ENTRYPOINT ["python3", "-I", "-B", "/work/source/packaging/deb_ubuntu.py", "--source", "/work/source", "--build-dir", "/work/build", "--out-dir", "/work/out"]
