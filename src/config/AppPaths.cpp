#include "config/AppPaths.hpp"

#include <SDL.h>

#include <filesystem>

namespace launcher::config {
namespace {
namespace fs = std::filesystem;

std::string preferenceRoot(const char *product) {
  char *path = SDL_GetPrefPath("MRG", product);
  if (path == nullptr)
    return {};
  std::string result(path);
  SDL_free(path);
  return result;
}

bool copyIfMissing(const fs::path &source, const fs::path &destination,
                   std::string &error) {
  std::error_code ec;
  if (!fs::exists(source, ec) || ec || fs::exists(destination, ec))
    return !ec;
  fs::create_directories(destination.parent_path(), ec);
  if (ec) {
    error = "Cannot create migrated data directory: " + ec.message();
    return false;
  }
  if (fs::is_directory(source, ec)) {
    fs::copy(source, destination,
             fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
  } else {
    fs::copy_file(source, destination, fs::copy_options::skip_existing, ec);
  }
  if (ec) {
    error = "Cannot migrate legacy launcher data: " + ec.message();
    return false;
  }
  return true;
}

} // namespace

std::string dataRoot() { return preferenceRoot("MRGPlayer"); }

std::string legacyDataRoot() { return preferenceRoot("NESPlayerLauncher"); }

bool migrateLegacyData(std::string &error) {
  error.clear();
  const fs::path current(dataRoot());
  const fs::path legacy(legacyDataRoot());
  if (current.empty()) {
    error = std::string("Cannot locate MRG Player data directory: ") +
            SDL_GetError();
    return false;
  }
  std::error_code ec;
  fs::create_directories(current, ec);
  if (ec) {
    error = "Cannot create MRG Player data directory: " + ec.message();
    return false;
  }
  if (legacy.empty() || !fs::exists(legacy, ec) || ec)
    return !ec;
  return copyIfMissing(legacy / "settings.ini", current / "settings.ini",
                       error) &&
         copyIfMissing(legacy / "roms", current / "library" / "nes", error) &&
         copyIfMissing(legacy / "saves", current / "saves" / "nes", error);
}

std::string dataSubdirectory(std::string_view first, std::string_view second) {
  std::string migrationError;
  (void)migrateLegacyData(migrationError);
  const std::string root = dataRoot();
  const fs::path base = root.empty() ? fs::path(".") : fs::path(root);
  const fs::path directory = base / std::string(first) / std::string(second);
  std::error_code ec;
  fs::create_directories(directory, ec);
  return directory.string();
}

} // namespace launcher::config
