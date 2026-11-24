#include "runtime/RuntimeRegistry.hpp"

#include <filesystem>
#include <system_error>

#include "config/AppPaths.hpp"
#include "game/GamePlatform.hpp"
#include "library/GameLibrary.hpp"
#include "runtime/IGameRuntime.hpp"
#include "runtime/libretro/CoreDiscovery.hpp"
#include "runtime/libretro/LibretroRuntime.hpp"

namespace launcher::runtime {
namespace {
namespace fs = std::filesystem;

// Battery RAM of a libretro core, named after the game's stable content id so
// reimporting the same file keeps its save.
std::string batterySavePath(const library::GameEntry &game, std::string_view directoryOverride) {
  const std::string directory = directoryOverride.empty()
                                    ? config::dataSubdirectory(
                                          "saves", std::string(game::platformDescriptor(game.platform).managedDirectory))
                                    : std::string(directoryOverride);
  return (fs::path(directory) / (game.id + ".srm")).string();
}

} // namespace

bool RuntimeRegistry::supports(game::GamePlatform platform) const noexcept {
  return libretro::servedByCore(platform);
}

std::unique_ptr<IGameRuntime> RuntimeRegistry::create(game::GamePlatform platform) const {
  if (libretro::servedByCore(platform))
    return std::make_unique<libretro::LibretroRuntime>();
  return {};
}

SaveDataStatus RuntimeRegistry::inspectSaveData(const library::GameEntry &game, std::string &error,
                                                std::string_view directoryOverride) const {
  if (!supports(game.platform)) {
    error = "No installed runtime manages save data for this platform";
    return SaveDataStatus::Unsupported;
  }
  const std::string path = batterySavePath(game, directoryOverride);
  std::error_code failure;
  const bool present = fs::is_regular_file(path, failure);
  if (failure == std::errc::no_such_file_or_directory) {
    error.clear();
    return SaveDataStatus::Missing;
  }
  if (failure) {
    error = "Cannot inspect save data: " + failure.message();
    return SaveDataStatus::Failed;
  }
  error.clear();
  return present ? SaveDataStatus::Present : SaveDataStatus::Missing;
}

SaveDataStatus RuntimeRegistry::deleteSaveData(const library::GameEntry &game, std::string &error,
                                               std::string_view directoryOverride) const {
  if (!supports(game.platform)) {
    error = "No installed runtime manages save data for this platform";
    return SaveDataStatus::Unsupported;
  }
  const std::string path = batterySavePath(game, directoryOverride);
  std::error_code failure;
  const bool present = fs::is_regular_file(path, failure);
  if (failure == std::errc::no_such_file_or_directory) {
    error.clear();
    return SaveDataStatus::Missing;
  }
  if (failure) {
    error = "Cannot inspect save data: " + failure.message();
    return SaveDataStatus::Failed;
  }
  if (!present) {
    error.clear();
    return SaveDataStatus::Missing;
  }
  if (!fs::remove(path, failure) || failure) {
    error = "Cannot remove save data: " + failure.message();
    return SaveDataStatus::Failed;
  }
  error.clear();
  return SaveDataStatus::Removed;
}

} // namespace launcher::runtime
