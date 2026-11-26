Put ROMs that ship with the pak here; see docs/brick-pro-porting.md section 6.4
for licensing.

The launcher imports everything it finds in this folder on first start (and
again whenever the Home screen's import action is used on a device without a
file dialog). Subfolders such as roms/nes/ are searched too. The card's own
/SDCARD/Roms tree is scanned the same way; pack/launch.sh points MRG_ROM_DIRS at
both.
