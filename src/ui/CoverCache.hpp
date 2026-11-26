#pragma once

#include <SDL.h>

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "library/CoverNames.hpp"
#include "library/GameLibrary.hpp"
#include "ui/CoverSource.hpp"

namespace launcher::ui {

// Box art for the shelf. Covers live as plain PNG files in <data>/covers, so a
// player can drop one in by hand and keep it offline. When a cover is missing
// the cache asks the libretro thumbnail repositories once, in the background,
// and silently gives up when the network is not there.
class CoverCache : public CoverSource {
 public:
  CoverCache() = default;
  ~CoverCache();
  CoverCache(const CoverCache &) = delete;
  CoverCache &operator=(const CoverCache &) = delete;

  // Creates the directory, writes covers/names.json the first time, and loads
  // the name mapping. A malformed mapping is reported and then ignored.
  // cardRoots are the Imgs/ folders that came with the games; art found there
  // is used as is and nothing is downloaded for that game.
  void initialize(const std::string &directory, std::vector<std::string> cardRoots = {});

  // Joins the worker and drops every texture. Must run before the renderer.
  void shutdown();

  // The cover for a game, or nullptr while there is none yet. The first call
  // for a game with no cached file starts a download.
  [[nodiscard]] SDL_Texture *cover(SDL_Renderer *renderer, const library::GameEntry &game) override;

  // The same image, blurred, for the home wallpaper. Null when no cover is
  // cached.
  [[nodiscard]] SDL_Texture *wallpaper(SDL_Renderer *renderer, const library::GameEntry &game) override;

  // Installs a finished download. Call once per frame, outside drawing.
  void poll(SDL_Renderer *renderer);

  [[nodiscard]] bool downloading() const noexcept { return job_ != nullptr && !job_->done.load(); }
  [[nodiscard]] const std::string &directory() const noexcept { return directory_; }

 private:
  // A download outlives a quick quit: the worker holds this record instead of
  // the cache, so shutdown can let go instead of waiting on a dead socket.
  struct DownloadJob {
    std::atomic<bool> done{false};
    bool ok = false;
    std::string id;
  };

  [[nodiscard]] std::string pathFor(const std::string &id) const;
  // The art that came with the game on a card, or an empty string when neither
  // the card nor its Imgs/ folder has one. The answer is remembered per game so
  // a card is not probed again on every frame.
  [[nodiscard]] std::string cardCoverPath(const library::GameEntry &game);
  [[nodiscard]] SDL_Texture *loadCover(SDL_Renderer *renderer, const std::string &id,
                                       const std::string &path);
  [[nodiscard]] SDL_Texture *loadWallpaper(SDL_Renderer *renderer, const std::string &id,
                                           const std::string &path);
  void startDownload(const library::GameEntry &game);

  std::string directory_;
  std::vector<std::string> cardRoots_;
  std::map<std::string, std::string> cardCovers_;
  library::CoverNames names_;
  std::map<std::string, SDL_Texture *> covers_;
  std::map<std::string, SDL_Texture *> wallpapers_;
  std::set<std::string> attempted_;
  std::thread worker_;
  std::shared_ptr<DownloadJob> job_;
  // A card asks for its cover while it is being drawn, and a thread should not
  // be started in the middle of a frame, so the request waits here until the
  // next poll.
  library::GameEntry pendingGame_;
};

}  // namespace launcher::ui
