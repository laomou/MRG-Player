#!/bin/sh
# The stock desktop hands the ROM over as a bare path and expects the emulator
# to exit once the player leaves the game, which is exactly what the player
# does with a ROM argument. No RetroArch in the middle.
SDCARD=${SDCARD_PATH:-/mnt/SDCARD}
exec "$SDCARD/Apps/MRG.pak/launch.sh" "$*"
