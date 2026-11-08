#!/usr/bin/env bash

# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2026 Nitrux

set -euo pipefail

if (( EUID == 0 )); then
    SUDO=()
else
    SUDO=(sudo)
fi

"${SUDO[@]}" apt-get update -q
"${SUDO[@]}" apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    dpkg-dev \
    extra-cmake-modules \
    gnupg \
    libkf6coreaddons-dev \
    libpolkit-gobject-1-dev \
    libpwquality-dev \
    pkg-config \
    qt6-base-dev \
    qt6-base-dev-tools \
    qt6-declarative-dev \
    qt6-declarative-dev-tools

KEYRING_DIR=/etc/apt/keyrings
KEYRING_PATH="$KEYRING_DIR/nitrux-mauikit.gpg"
SOURCE_PATH=/etc/apt/sources.list.d/nitrux-mauikit.sources

"${SUDO[@]}" install -d -m 0755 "$KEYRING_DIR"
curl -fsSL https://packagecloud.io/nitrux/mauikit/gpgkey \
    | "${SUDO[@]}" gpg --dearmor --yes -o "$KEYRING_PATH"
"${SUDO[@]}" chmod 0644 "$KEYRING_PATH"
"${SUDO[@]}" tee "$SOURCE_PATH" >/dev/null <<'SOURCE'
Types: deb
Description: Nitrux MauiKit repository
URIs: https://packagecloud.io/nitrux/mauikit/debian/
Suites: duke
Components: main
Signed-By: /etc/apt/keyrings/nitrux-mauikit.gpg
Enabled: yes
SOURCE

"${SUDO[@]}" apt-get update -q
"${SUDO[@]}" apt-get install -y --no-install-recommends mauikit
