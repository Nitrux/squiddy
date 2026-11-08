#!/usr/bin/env bash

# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2026 Nitrux

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_version="${PACKAGE_VERSION:?PACKAGE_VERSION must be supplied}"
target_arch="${TARGET_ARCH:-$(dpkg --print-architecture)}"
host_multiarch="${DEB_HOST_MULTIARCH:-$(dpkg-architecture -qDEB_HOST_MULTIARCH)}"
build_dir="$repo_root/build"
cmake_build_dir="$build_dir/cmake"

if [[ ! "$package_version" =~ ^[0-9][A-Za-z0-9.+:~_-]*$ ]]; then
    printf 'Invalid Debian package version: %s\n' "$package_version" >&2
    exit 2
fi

if [[ "$target_arch" != "$(dpkg --print-architecture)" ]]; then
    printf 'TARGET_ARCH (%s) must match the native build architecture (%s).\n' \
        "$target_arch" "$(dpkg --print-architecture)" >&2
    exit 2
fi

rm -rf "$cmake_build_dir"
mkdir -p "$cmake_build_dir" "$build_dir"
cmake -S "$repo_root" -B "$cmake_build_dir" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DKDE_INSTALL_LIBDIR="lib/$host_multiarch" \
    -DKDE_INSTALL_SYSCONFDIR=/etc \
    -DKDE_INSTALL_LOCALSTATEDIR=/var \
    -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON \
    -DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCPACK_PACKAGE_VERSION="$package_version" \
    -DCPACK_DEBIAN_PACKAGE_ARCHITECTURE="$target_arch" \
    -DCPACK_PACKAGE_DIRECTORY="$build_dir"

cmake --build "$cmake_build_dir" --parallel "$(nproc)"
cpack --config "$cmake_build_dir/CPackConfig.cmake" -G DEB

printf 'Debian package created in %s\n' "$build_dir"
