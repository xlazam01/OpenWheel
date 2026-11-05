# Build and test environment for indi_openwheel: Ubuntu 24.04 with libindi 2.x built from source
# (Ubuntu's own libindi-dev is 1.9.9, and the INDI PPA has no libindi build for 24.04 at the moment).
#   docker build -t openwheel-indi-build driver/INDI/docker
#   docker run --rm -v "$PWD/driver/INDI:/src:ro" openwheel-indi-build /src/docker/build-and-test.sh
FROM ubuntu:24.04
ARG INDI_VERSION=v2.2.5
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config git ca-certificates procps \
      libnova-dev libcfitsio-dev zlib1g-dev libusb-1.0-0-dev libudev-dev libev-dev \
      libgsl-dev libjpeg-dev libcurl4-gnutls-dev libtheora-dev libfftw3-dev \
 && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --branch ${INDI_VERSION} https://github.com/indilib/indi.git /tmp/indi \
 && cmake -S /tmp/indi -B /tmp/indi/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DINDI_BUILD_UNITTESTS=OFF -DINDI_BUILD_INTEGTESTS=OFF -DINDI_BUILD_XISF=OFF -DFIX_WARNINGS=OFF \
 && cmake --build /tmp/indi/build -j"$(nproc)" \
 && cmake --install /tmp/indi/build \
 && ldconfig \
 && rm -rf /tmp/indi
