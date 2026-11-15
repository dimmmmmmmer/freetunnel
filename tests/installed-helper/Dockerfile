ARG UBUNTU_VERSION=24.04
FROM ubuntu:${UBUNTU_VERSION}
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    cmake g++ ninja-build libssl-dev openssl python3 sudo iproute2 \
    libgl-dev libglx-dev libopengl-dev libegl-dev libxkbcommon0 libfontconfig1 libglib2.0-0 \
    libdbus-1-3 libxcb1 libx11-6 libx11-xcb1 libpcre2-16-0 libicu-dev \
    && rm -rf /var/lib/apt/lists/*
