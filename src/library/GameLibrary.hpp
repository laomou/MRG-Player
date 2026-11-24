#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "game/GamePlatform.hpp"
#include "runtime/VideoOptions.hpp"

namespace launcher::library {

struct GameEntry {
  std::string id;
  std::string title;
  std::string filePath;
  game::GamePlatform platform = game::GamePlatform::Unknown;
  bool favorite = false;
  std::uint64_t playSeconds = 0;
  std::int64_t lastPlayed = 0;
};

enum class ImportStatus { Imported, AlreadyPresent, Failed };

class GameLibrary {
public:
  explicit GameLibrary(std::string directoryOverride = {});

  bool initialize();
  ImportStatus importGame(const std::string &sourcePath);
  bool removeGame(std::size_t index);
  bool setFavorite(std::size_t index, bool favorite);
  bool setCustomTitle(std::size_t index, std::string title);
  bool recordPlaySession(const std::string &id, game::GamePlatform platform,
                         std::uint64_t seconds, std::int64_t lastPlayed);

  // Per-game video overrides fall back to the global settings when absent.
  [[nodiscard]] bool videoOverride(
      const std::string &id, game::GamePlatform platform,
      runtime::VideoOptions &options) const noexcept;
  bool setVideoOverride(const std::string &id,
                        game::GamePlatform platform,
                        const runtime::VideoOptions &options);
  bool clearVideoOverride(const std::string &id, game::GamePlatform platform);

  [[nodiscard]] const std::vector<GameEntry> &entries() const noexcept;
  [[nodiscard]] std::size_t slotCount() const noexcept;
  [[nodiscard]] const std::string &directory() const noexcept;
  [[nodiscard]] std::string_view error() const noexcept;
  [[nodiscard]] int indexOfPath(const std::string &path) const noexcept;
  [[nodiscard]] int indexOfId(const std::string &id,
                              game::GamePlatform platform) const noexcept;
  [[nodiscard]] int lastImportedIndex() const noexcept;

private:
  struct Metadata {
    bool favorite = false;
    std::uint64_t playSeconds = 0;
    std::int64_t lastPlayed = 0;
    bool hasVideoOverride = false;
    runtime::VideoOptions video;
    std::string customTitle;
  };

  bool refresh();
  bool loadMetadata();
  bool saveMetadata();
  [[nodiscard]] static std::string metadataKey(const std::string &id,
                                               game::GamePlatform platform);

  std::string directoryOverride_;
  std::string directory_;
  std::string error_;
  std::vector<GameEntry> entries_;
  std::unordered_map<std::string, Metadata> metadata_;
  int lastImportedIndex_ = -1;
};

} // namespace launcher::library
