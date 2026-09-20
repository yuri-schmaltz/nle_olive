#!/usr/bin/env bash
# Ubuntu 24.04 / Mint 22 development dependencies; run explicitly with sudo.
set -euo pipefail
apt-get update
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build git python3 pkg-config \
  qt6-base-dev qt6-tools-dev qt6-tools-dev-tools libqt6opengl6-dev \
  libopencolorio-dev libopenimageio-dev openimageio-tools libopenexr-dev libimath-dev \
  libavcodec-dev libavformat-dev libavfilter-dev libavutil-dev libswscale-dev libswresample-dev \
  portaudio19-dev libgl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0 \
  ffmpeg xvfb xauth mesa-utils
