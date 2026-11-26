#!/bin/sh
# MRG Player - TrimUI Brick Pro launcher.
#
# Copy this whole directory into /SDCARD/Apps/ so the layout is
# /SDCARD/Apps/MRG.pak/config.json. The card is FAT32/exFAT, so nothing here
# needs an executable bit (or survives one).

progdir=$(cd "$(dirname "$0")" && pwd)
SDCARD=${SDCARD_PATH:-/mnt/SDCARD}

# The firmware ships SDL2 and SDL2_ttf, so only a pak that bundles extra
# libraries needs this line. It is harmless otherwise.
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$progdir

# Cores and the bundled ROMs travel with the pak.
export MRG_CORES_DIR=$progdir/cores

# User data (settings, saves, screenshots) lives outside the pak so replacing
# it on update never costs anyone a save file.
export MRG_DATA_DIR=$SDCARD/.userdata/mrg
mkdir -p "$MRG_DATA_DIR"

# Where imported games are looked for. The ROMs shipped with the pak sit next
# to the binary; the card's own tree is the usual place for everything else.
# The launcher imports whatever it finds here on first start, and the Home
# screen's import action rescans these folders because the device has no file
# dialog.
export MRG_ROM_DIRS=$progdir/roms:$SDCARD/Roms

cd $progdir || exit 1
# exec keeps the exit status flowing back to MainUI; do not touch governor,
# tinymix, rfkill or the LED here, the system owns those.
exec ./mrg-player "$@"
