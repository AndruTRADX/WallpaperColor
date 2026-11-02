#!/usr/bin/env bash
# Builds the portable release tarball: dist/wallpapercolor-<version>-x86_64.tar.gz (+ .sha256).
# The binaries only need glibc and libsystemd, so the tarball runs on any systemd distribution
# with a glibc at least as new as the one this is built on (CI uses Ubuntu 24.04: glibc 2.39).
# Usage: packaging/release.sh        (CC / CXX pick the compiler, GCC 14 or newer)
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."

version=$(sed -nE 's/^project\(WallpaperColor VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
arch=$(uname -m)
name="wallpapercolor-$version-$arch"

cmake -S . -B build-portable -G Ninja -DCMAKE_BUILD_TYPE=Release -DWPC_PORTABLE=ON
cmake --build build-portable
ctest --test-dir build-portable --output-on-failure

stage="build-portable/$name"
rm -rf "$stage" && mkdir -p "$stage"
install -m755 -s build-portable/src/wallpapercolor  "$stage/wallpapercolor"
install -m755 -s build-portable/src/wallpapercolord "$stage/wallpapercolord"
install -m644 systemd/wallpapercolord.service "$stage/"
install -m644 udev/70-wallpapercolor.rules "$stage/"
install -m755 packaging/install.sh "$stage/install.sh"
install -m644 README.md "$stage/"

mkdir -p dist
tar -C build-portable -czf "dist/$name.tar.gz" --owner=0 --group=0 "$name"
(cd dist && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")

echo "dist/$name.tar.gz"
echo "Needs glibc $(objdump -T "$stage/wallpapercolord" "$stage/wallpapercolor" |
  grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 | cut -d_ -f2) or newer."
