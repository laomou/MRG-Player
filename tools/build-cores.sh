#!/usr/bin/env bash
# Builds the libretro cores the TrimUI Brick package ships.
#
# The buildbot publishes aarch64 builds, but they are linked against glibc
# 2.33+ (measured: fceumm 2.33, mgba 2.35) while the Brick firmware ships an
# older one, so dlopen fails on device. Cross-building with the same toolchain
# as the launcher pins every core to GLIBC_2.17.
#
# Usage:
#   tools/build-cores.sh --prefix <cross-prefix> --sysroot <sysroot> \
#                        [--out DIR] [--src DIR] [--jobs N] [core...]
#
# core: fceumm | mgba | snes9x | genesis_plus_gx (default: all four)
set -euo pipefail

CROSS_PREFIX=""
SYSROOT=""
OUT=""
SRC="${TMPDIR:-/tmp}/mrg-cores-src"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
CORES=""

usage() { sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) CROSS_PREFIX="$2"; shift 2 ;;
    --sysroot) SYSROOT="$2"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --src) SRC="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    -*) echo "unknown option: $1" >&2; usage; exit 2 ;;
    *) CORES="$CORES $1"; shift ;;
  esac
done

[ -n "$CROSS_PREFIX" ] || { echo "--prefix is required" >&2; exit 2; }
[ -n "$SYSROOT" ] || { echo "--sysroot is required" >&2; exit 2; }
[ -n "$OUT" ] || { echo "--out is required" >&2; exit 2; }
CORES="${CORES:- fceumm mgba snes9x genesis_plus_gx}"

# Pinned upstream commits. Bump deliberately: a core that stops building is a
# release blocker, and an unpinned HEAD turns that into a surprise on tag day.
FCEUMM_REV=fb919fb6fa881aaa6279b765d480220579e5c606
MGBA_REV=7a12d6d4b9acb14c0ae62c9166b6a2f3d08007f6
SNES9X_REV=204ea1c0b851f960cbf223075eb1c25641cb483c
GENPLUS_REV=51348f65738b0c75f4b8b4488fbc67645e3769af

CC_BIN="${CROSS_PREFIX}gcc"
CXX_BIN="${CROSS_PREFIX}g++"
for tool in gcc g++ ar ranlib strip; do
  [ -x "${CROSS_PREFIX}${tool}" ] || { echo "missing cross tool: ${CROSS_PREFIX}${tool}" >&2; exit 2; }
done

export CC="${CC_BIN} --sysroot=${SYSROOT}"
export CXX="${CXX_BIN} --sysroot=${SYSROOT}"
export AR="${CROSS_PREFIX}ar"
export RANLIB="${CROSS_PREFIX}ranlib"
export STRIP="${CROSS_PREFIX}strip"
export CFLAGS="-O2 -fPIC -pipe"
export CXXFLAGS="-O2 -fPIC -pipe"
# Two of the cores are C++: linking libstdc++ statically keeps the package free
# of a libstdc++ version the firmware may not have.
# The toolchain ships a newer libm than the firmware, and the linker
# resolves exp2f/powf to it (GLIBC_2.27) unless the sysroot copy comes
# first. Naming it pins the cores and the launcher to GLIBC_2.17.
SYSROOT_LIBM=""
if [ -f "$SYSROOT/usr/lib/libm.so.6" ]; then SYSROOT_LIBM="$SYSROOT/usr/lib/libm.so.6"; fi
export LDFLAGS="-static-libstdc++ -static-libgcc $SYSROOT_LIBM"

# Fetch a repository at an exact commit. --depth 1 of the SHA keeps the cache
# small; GitHub serves arbitrary reachable SHAs.
fetch() {
  local name="$1" url="$2" rev="$3" dir="$SRC/$1"
  if [ -d "$dir/.git" ] && [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" = "$rev" ]; then
    return 0
  fi
  rm -rf "$dir"
  mkdir -p "$dir"
  git -C "$dir" init -q
  git -C "$dir" remote add origin "$url"
  git -C "$dir" fetch -q --depth 1 origin "$rev"
  git -C "$dir" checkout -q FETCH_HEAD
}

build_fceumm() {
  fetch fceumm https://github.com/libretro/libretro-fceumm.git "$FCEUMM_REV"
  make -C "$SRC/fceumm" -j"$JOBS" platform=unix >/dev/null
  install_core "$SRC/fceumm/fceumm_libretro.so" fceumm_libretro.so
}

build_mgba() {
  fetch mgba https://github.com/libretro/mgba.git "$MGBA_REV"
  local build="$SRC/mgba/build-brick"
  rm -rf "$build"
  cmake -S "$SRC/mgba" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_C_COMPILER="$CC_BIN" \
    -DCMAKE_CXX_COMPILER="$CXX_BIN" \
    -DCMAKE_SYSROOT="$SYSROOT" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DBUILD_LIBRETRO=ON -DBUILD_QT=OFF -DBUILD_SDL=OFF \
    -DUSE_FFMPEG=OFF -DUSE_PNG=OFF -DUSE_ZLIB=OFF -DUSE_MINIZIP=OFF \
    -DUSE_LIBZIP=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_EPOXY=OFF \
    -DUSE_DISCORD_RPC=OFF -DUSE_LUA=OFF -DUSE_FFTW3=OFF \
    -DCMAKE_SHARED_LINKER_FLAGS="$SYSROOT_LIBM" >/dev/null
  cmake --build "$build" --parallel "$JOBS" >/dev/null
  install_core "$(find "$build" -name '*libretro*.so' | head -1)" mgba_libretro.so
}

build_snes9x() {
  fetch snes9x https://github.com/libretro/snes9x.git "$SNES9X_REV"
  make -C "$SRC/snes9x/libretro" -j"$JOBS" platform=unix >/dev/null
  install_core "$SRC/snes9x/libretro/snes9x_libretro.so" snes9x_libretro.so
}

build_genesis_plus_gx() {
  fetch genesis_plus_gx https://github.com/libretro/Genesis-Plus-GX.git "$GENPLUS_REV"
  make -C "$SRC/genesis_plus_gx" -j"$JOBS" -f Makefile.libretro platform=unix >/dev/null
  install_core "$SRC/genesis_plus_gx/genesis_plus_gx_libretro.so" genesis_plus_gx_libretro.so
}

install_core() {
  local from="$1" name="$2"
  [ -f "$from" ] || { echo "build produced no $name" >&2; exit 1; }
  mkdir -p "$OUT"
  "$STRIP" --strip-unneeded -o "$OUT/$name" "$from"
  printf '  %-28s %9s B\n' "$name" "$(wc -c <"$OUT/$name")"
}

mkdir -p "$SRC" "$OUT"
for core in $CORES; do
  case "$core" in
    fceumm) echo "building fceumm"; build_fceumm ;;
    mgba) echo "building mgba"; build_mgba ;;
    snes9x) echo "building snes9x"; build_snes9x ;;
    genesis_plus_gx) echo "building genesis_plus_gx"; build_genesis_plus_gx ;;
    *) echo "unknown core: $core" >&2; exit 2 ;;
  esac
done

echo "cores written to $OUT"
