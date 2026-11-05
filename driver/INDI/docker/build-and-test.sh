#!/bin/sh
# Configure, build and test inside the build container. Extra arguments go to cmake,
# e.g. -DOPENWHEEL_BUILD_DRIVER=OFF to build only the core, simulator and core tests.
set -e
SRC=${SRC:-/src}
BUILD=${BUILD:-/tmp/build}
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@"
cmake --build "$BUILD" -j"$(nproc)"
cd "$BUILD"
ctest --output-on-failure
