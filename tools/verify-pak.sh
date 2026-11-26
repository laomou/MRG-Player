#!/bin/sh
# Checks that a built MRG.pak can actually run on the Brick Pro: only libraries
# the firmware ships, and no symbol newer than the glibc the oldest known
# firmware provides. Run from CI, needs no device.
#
#   tools/verify-pak.sh build-brick/pak/MRG.pak [readelf] [objdump]
#
# Every bundled core is checked the same way as the launcher: a core built
# against a newer libc only fails at dlopen time, on the device.

set -eu

pakdir=${1:?usage: verify-pak.sh <pak dir> [readelf] [objdump]}
readelf=${2:-readelf}
objdump=${3:-objdump}
exe="$pakdir/mrg-player"

[ -f "$exe" ] || { echo "verify-pak: missing $exe" >&2; exit 1; }

fail=0

# libSDL2, libSDL2_ttf, zlib, and the C/C++ runtime pieces the firmware already
# provides. Anything else would have to be shipped inside the pak.
allowed="libSDL2-2.0.so.0 libSDL2_ttf-2.0.so.0 libz.so.1 libdl.so.2 libpthread.so.0 libm.so.6 libc.so.6 libgcc_s.so.1"

check_cap() {
  # $1 = observed version, $2 = ceiling, $3 = label, $4 = binary name
  top=$(printf "%s\n%s\n" "$1" "$2" | sort -Vu | tail -1)
  if [ "$top" != "$2" ]; then
    echo "verify-pak: $4 $3 $1 is above the supported $2" >&2
    fail=1
  fi
}

check_binary() {
  # $1 = file, $2 = display name
  file="$1"
  name="$2"
  if ! "$readelf" -d "$file" >/dev/null 2>&1; then
    echo "verify-pak: $name is not a readable shared object" >&2
    fail=1
    return
  fi

  needed=$("$readelf" -d "$file" | sed -n "s/.*NEEDED.*\[\(.*\)\]/\1/p" | sort -u)
  printf "NEEDED (%s):\n%s\n" "$name" "$needed" | sed "s/^/  /"
  for lib in $needed; do
    case " $allowed " in
      *" $lib "*) ;;
      *) echo "verify-pak: $name depends on $lib, which the firmware does not ship" >&2; fail=1 ;;
    esac
  done

  # The oldest firmware we support reports GLIBC_2.17, the same floor MinUI
  # targets; a symbol above it makes the loader refuse the binary outright.
  binary_glibc=$("$objdump" -T "$file" | grep -o "GLIBC_[0-9.]*" | sed "s/GLIBC_//" | sort -Vu | tail -1)
  binary_glibcxx=$("$objdump" -T "$file" | grep -o "GLIBCXX_[0-9.]*" | sed "s/GLIBCXX_//" | sort -Vu | tail -1)
  binary_glibc=${binary_glibc:-0}
  binary_glibcxx=${binary_glibcxx:-0}
  echo "$name: GLIBC max $binary_glibc, GLIBCXX max $binary_glibcxx"
  check_cap "$binary_glibc" "2.17" "GLIBC" "$name"
  check_cap "$binary_glibcxx" "3.4.21" "GLIBCXX" "$name"
}

check_binary "$exe" "mrg-player"

corecount=0
for core in "$pakdir"/cores/*.so; do
  [ -f "$core" ] || continue
  corecount=$((corecount + 1))
  check_binary "$core" "cores/$(basename "$core")"
done
echo "cores checked: $corecount"

# The pak ships exactly these cores. A build that silently lost one would pass
# every check above and then fail to start that platform on the device.
for core in fceumm_libretro.so mgba_libretro.so snes9x_libretro.so genesis_plus_gx_libretro.so; do
  if [ ! -f "$pakdir/cores/$core" ]; then
    echo "verify-pak: missing core cores/$core" >&2
    fail=1
  fi
done

# The engine exits when the font is missing, so a pak that lost res/fonts/
# would show nothing at all instead of a partial UI.
for font in DroidSansFallbackFull.ttf DejaVuSans.ttf; do
  if [ ! -f "$pakdir/res/fonts/$font" ]; then
    echo "verify-pak: missing font res/fonts/$font" >&2
    fail=1
  fi
done

if [ "$fail" -ne 0 ]; then
  echo "verify-pak: FAILED" >&2
  exit 1
fi
echo "verify-pak: ok"
