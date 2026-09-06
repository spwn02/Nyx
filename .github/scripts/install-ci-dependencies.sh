#!/usr/bin/env bash
set -euo pipefail

: "${RUNNER_TEMP:?RUNNER_TEMP must be set by GitHub Actions}"
: "${GITHUB_PATH:?GITHUB_PATH must be set by GitHub Actions}"
: "${GITHUB_ENV:?GITHUB_ENV must be set by GitHub Actions}"
: "${GITHUB_WORKSPACE:?GITHUB_WORKSPACE must be set by GitHub Actions}"

sudo apt-get update
sudo apt-get install --yes --no-install-recommends \
  autoconf \
  autoconf-archive \
  automake \
  bison \
  build-essential \
  ca-certificates \
  curl \
  flex \
  gh \
  git \
  libasound2-dev \
  libdbus-1-dev \
  libegl1-mesa-dev \
  libgl-dev \
  libpulse-dev \
  libtool \
  libudev-dev \
  libwayland-dev \
  libx11-dev \
  libx11-xcb-dev \
  libxcb1-dev \
  libxcursor-dev \
  libxext-dev \
  libxfixes-dev \
  libxi-dev \
  libxinerama-dev \
  libxkbcommon-dev \
  libxkbcommon-x11-dev \
  libxrandr-dev \
  libxss-dev \
  libxtst-dev \
  nasm \
  ninja-build \
  pkg-config \
  python3 \
  python3-venv \
  unzip \
  wayland-protocols \
  zip \
  zstd

python3 -m venv "$RUNNER_TEMP/cmake"
"$RUNNER_TEMP/cmake/bin/pip" install --no-cache-dir cmake==4.4.2

wheelhouse="$GITHUB_WORKSPACE/.cache/python-wheelhouse"
mkdir -p "$wheelhouse"
"$RUNNER_TEMP/cmake/bin/pip" download \
  --only-binary=:all: \
  --require-hashes \
  --dest "$wheelhouse" \
  --no-deps \
  -r "$GITHUB_WORKSPACE/.github/python-requirements.txt"

echo "PIP_NO_INDEX=1" >>"$GITHUB_ENV"
echo "PIP_FIND_LINKS=$wheelhouse" >>"$GITHUB_ENV"
echo "$RUNNER_TEMP/cmake/bin" >>"$GITHUB_PATH"
