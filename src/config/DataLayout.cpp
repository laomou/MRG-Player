#include "config/DataLayout.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "config/AppPaths.hpp"
#include "game/GamePlatform.hpp"
#include "util/Utf8Path.hpp"

namespace launcher::config {
namespace {
namespace fs = std::filesystem;

// Two spellings of one folder: a card formatted case-insensitively folds FC and
// fc together, and moving a folder onto itself would lose it.
bool sameDirectory(const fs::path &left, const fs::path &right) {
  std::error_code ec;
  if (fs::equivalent(left, right, ec) && !ec)
    return true;
  return paths::toUtf8(left) == paths::toUtf8(right);
}

// A rename is atomic and instant; a card that mounts the two folders from
// different filesystems refuses it, so a copy is the fallback.
bool moveEntry(const fs::path &from, const fs::path &to) {
  std::error_code ec;
  fs::rename(from, to, ec);
  if (!ec)
    return true;
  ec.clear();
  fs::copy_file(from, to, fs::copy_options::none, ec);
  if (ec)
    return false;
  ec.clear();
  fs::remove(from, ec);
  return true;
}

// Folds one file into a new home, doing nothing when either side is missing or
// the target already has a file: the newer copy wins.
void moveFile(const fs::path &source, const fs::path &target) {
  std::error_code ec;
  if (!fs::is_regular_file(source, ec) || ec)
    return;
  if (fs::exists(target, ec))
    return;
  fs::create_directories(target.parent_path(), ec);
  ec.clear();
  moveEntry(source, target);
}

// Folds every file of source into target and removes source once it is empty.
// A file that already exists in target is kept there, so nothing written by the
// newer build is overwritten.
void mergeInto(const fs::path &source, const fs::path &target) {
  std::error_code ec;
  if (!fs::is_directory(source, ec) || ec)
    return;
  if (sameDirectory(source, target))
    return;
  fs::create_directories(target, ec);
  for (fs::directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path from = it->path();
    const fs::path to = target / from.filename();
    std::error_code existsEc;
    if (fs::exists(to, existsEc))
      continue;
    moveEntry(from, to);
  }
  ec.clear();
  if (fs::is_empty(source, ec) && !ec) {
    ec.clear();
    fs::remove(source, ec);
  }
}

// The per-platform folders of one class of data, under the old and the new
// name.
void migratePlatformFolders(const fs::path &root, std::string_view kind) {
  for (const game::PlatformDescriptor &descriptor : game::platformDescriptors()) {
    if (descriptor.legacyManagedDirectory.empty() ||
        descriptor.legacyManagedDirectory == descriptor.managedDirectory)
      continue;
    mergeInto(root / std::string(kind) / std::string(descriptor.legacyManagedDirectory),
              root / std::string(kind) / std::string(descriptor.managedDirectory));
  }
}

} // namespace

void migrateDataLayout() {
  const std::string root = dataRoot();
  if (root.empty())
    return;
  const fs::path base = paths::fromUtf8(root);

  // The library holds a managed copy of every imported ROM, so the same folder
  // names appear in all three trees and move together.
  constexpr std::array<std::string_view, 3> kManagedTrees{"library", "saves", "states"};
  for (std::string_view kind : kManagedTrees)
    migratePlatformFolders(base, kind);

  // Extracted cores used to sit beside the archives; they now sit inside the
  // folder that holds those archives, so one tree can be copied or deleted as a
  // whole.
  mergeInto(base / "cores-unpacked", base / "cores" / "unpacked");

  // cover-names.json describes the library, so it moved next to it instead of
  // crowding the root beside settings.ini.
  moveFile(base / "cover-names.json", base / "library" / "cover-names.json");
}

void clearRomCache() {
  const std::string root = dataRoot();
  if (root.empty())
    return;
  std::error_code ec;
  fs::remove_all(paths::fromUtf8(root) / "rom-cache", ec);
}

} // namespace launcher::config
