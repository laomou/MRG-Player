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

// Box art for the shelf. A cover is one PNG file in the card's own tree,
// Imgs/<system>/<title>.png, which is exactly the file the stock launcher shows
// for that ROM: art dropped in by hand, shipped with an image pack, or
// downloaded here is a single picture instead of two that can disagree. When
// the file is missing the cache asks the libretro thumbnail repositories once,
// in the background, and writes whatever comes back into the same place.
class CoverCache : public CoverSource {
 public:
  CoverCache() = default;
  ~CoverCache();
  CoverCache(const CoverCache &) = delete;
  CoverCache &operator=(const CoverCache &) = delete;

  // Loads the name mapping, writing cover-names.json the first time. A
  // malformed mapping is reported and then ignored. coverRoots are the Imgs/
  // folders paired with the ROM trees: art found there is used as is, and a
  // download is saved into the first one.
  void initialize(std::string namesPath, std::vector<std::string> coverRoots = {});

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

  // True once after a download failed because the host was unreachable. The
  // cache stops asking for the rest of the session; the caller can say so.
  [[nodiscard]] bool takeOfflineNotice();

  // Bumped whenever art appears, so a screen that only redraws on change knows
  // there is something new to show.
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

  [[nodiscard]] bool downloading() const noexcept { return job_ != nullptr && !job_->done.load(); }

 private:
  // A download outlives a quick quit: the worker holds this record instead of
  // the cache, so shutdown can let go instead of waiting on a dead socket.
  struct DownloadJob {
    std::atomic<bool> done{false};
    bool ok = false;
    bool networkFailure = false;
    std::string id;
    std::string target;
  };

  // The cover file that already exists for a game, or an empty string. The
  // answer is remembered per game so the folders are not probed on every frame.
  [[nodiscard]] std::string existingCoverPath(const library::GameEntry &game);
  // Where a new cover is written: Imgs/<system>/<title>.png under the first
  // root whose system folder exists, so a card keeps its art on the volume it
  // came from.
  [[nodiscard]] std::string coverTarget(const library::GameEntry &game) const;
  [[nodiscard]] SDL_Texture *loadCover(SDL_Renderer *renderer, const std::string &id,
                                       const std::string &path);
  [[nodiscard]] SDL_Texture *loadWallpaper(SDL_Renderer *renderer, const std::string &id,
                                           const std::string &path);
  void startDownload(const library::GameEntry &game);

  std::string namesPath_;
  std::vector<std::string> coverRoots_;
  std::map<std::string, std::string> coverPaths_;
  library::CoverNames names_;
  std::map<std::string, SDL_Texture *> covers_;
  std::map<std::string, SDL_Texture *> wallpapers_;
  std::set<std::string> attempted_;
  // A host that cannot be reached fails every later request the same way, so
  // one failure turns cover downloads off until the next launch.
  bool offline_ = false;
  bool noticePending_ = false;
  std::thread worker_;
  std::shared_ptr<DownloadJob> job_;
  std::uint64_t revision_ = 0;
  // A card asks for its cover while it is being drawn, and a thread should not
  // be started in the middle of a frame, so the request waits here until the
  // next poll.
  library::GameEntry pendingGame_;
};

}  // namespace launcher::ui
