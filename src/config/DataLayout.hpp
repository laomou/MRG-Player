#pragma once

namespace launcher::config {

// Moves the data an earlier build wrote into the folder names this one uses:
// the per-platform folders of the library, saves and instant states now carry
// the card's names (FC, GBA, ...) instead of the library ids (nes, gba, ...),
// unpacked cores live inside cores/, and cover-names.json sits in library/.
// Every step is idempotent and refuses to overwrite: a folder already in place
// is merged into, a file already in place is left alone, and a folder is
// removed only once it is empty. Safe to call on every start.
void migrateDataLayout();

// Drops the staged ROM copies a crash can leave behind. A run puts back the one
// it needs, so nothing of value is lost.
void clearRomCache();

} // namespace launcher::config
