FROM node:22-bullseye-slim AS node
FROM ubuntu:22.04

# Match the Linux CI dependencies, including Node for Docs_SiteAnalytics.
COPY --from=node /usr/local/bin/node /usr/local/bin/node
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential ca-certificates ninja-build git pkg-config python3 python3-pip file patchelf \
    xvfb xauth dbus-x11 fonts-dejavu-core fonts-noto-cjk libfuse2 \
    libgl1-mesa-dev libegl1-mesa-dev libgl1-mesa-dri libxkbcommon-dev libxkbcommon-x11-0 \
    libxcb-cursor0 libxcb-keysyms1-dev libxcb-xinerama0-dev libxcb-render-util0-dev \
    libxcb-image0-dev libxcb-icccm4-dev libxcb-randr0-dev libxcb-shape0-dev \
    libxcb-xfixes0-dev libxcb-sync-dev libxrender-dev libxi-dev libxtst-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    libpulse-dev libxcb-shm0-dev pulseaudio pulseaudio-utils \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 22.04 ships CMake 3.22. The Qt 6.12 SDK requires at least 3.25.
RUN python3 -m pip install --no-cache-dir cmake==3.31.6

# PulseAudio needs a user account. Match the host UID for writable reports.
ARG TEST_UID
RUN useradd --non-unique --uid "$TEST_UID" --gid users --create-home tester
ENV HOME=/home/tester
