#include "ui/CoverCache.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "config/AppPaths.hpp"
#include "util/Download.hpp"
#include "util/Image.hpp"
#include "util/Utf8Path.hpp"

namespace launcher::ui {
namespace {
namespace fs = std::filesystem;

// Box art is a few hundred kilobytes; eight megabytes leaves room for a
// generous scan without letting a stray file fill the disk.
constexpr std::uint64_t kMaximumCoverBytes = 8U * 1024U * 1024U;
constexpr int kWallpaperWidth = 56;
constexpr float kCornerRadiusFraction = 0.10f;

bool looksLikeImage(const std::vector<std::uint8_t> &bytes) {
  if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47)
    return true;
  return bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF;
}

void maskCorners(std::vector<std::uint8_t> &pixels, int width, int height) {
  const float radius = std::min(width, height) * kCornerRadiusFraction;
  if (radius < 1.0f)
    return;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float dx = x < radius ? radius - x : (x > width - radius ? x - (width - radius) : 0.0f);
      const float dy = y < radius ? radius - y : (y > height - radius ? y - (height - radius) : 0.0f);
      if (dx <= 0.0f || dy <= 0.0f)
        continue;
      const float distance = std::sqrt(dx * dx + dy * dy);
      const float coverage = std::clamp(radius - distance + 0.5f, 0.0f, 1.0f);
      std::uint8_t &alpha = pixels[static_cast<std::size_t>(y * width + x) * 4U + 3U];
      alpha = static_cast<std::uint8_t>(static_cast<float>(alpha) * coverage);
    }
  }
}

SDL_Texture *textureFromImage(SDL_Renderer *renderer, std::vector<std::uint8_t> &pixels, int width, int height) {
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), width, height, 32, width * 4,
                                                            SDL_PIXELFORMAT_RGBA32);
  if (surface == nullptr)
    return nullptr;
  SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (texture == nullptr)
    return nullptr;
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
  return texture;
}

SDL_Texture *createCoverTexture(SDL_Renderer *renderer, const io::Image &image, bool rounded) {
  std::vector<std::uint8_t> pixels = image.pixels;
  if (rounded)
    maskCorners(pixels, image.width, image.height);
  return textureFromImage(renderer, pixels, image.width, image.height);
}

// A 56-pixel-wide copy, stretched back up by the linear filter. That is far
// cheaper than a real blur and, seen behind a scrim, reads the same.
SDL_Texture *createWallpaperTexture(SDL_Renderer *renderer, const io::Image &image) {
  const int width = std::min(kWallpaperWidth, image.width);
  const int height = std::max(1, image.height * width / std::max(1, image.width));
  std::vector<std::uint8_t> small(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0);
  for (int y = 0; y < height; ++y) {
    const int sourceY0 = y * image.height / height;
    const int sourceY1 = std::max(sourceY0 + 1, (y + 1) * image.height / height);
    for (int x = 0; x < width; ++x) {
      const int sourceX0 = x * image.width / width;
      const int sourceX1 = std::max(sourceX0 + 1, (x + 1) * image.width / width);
      std::uint32_t red = 0;
      std::uint32_t green = 0;
      std::uint32_t blue = 0;
      std::uint32_t count = 0;
      for (int sy = sourceY0; sy < sourceY1; ++sy) {
        for (int sx = sourceX0; sx < sourceX1; ++sx) {
          const std::size_t offset = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width) +
                                      static_cast<std::size_t>(sx)) * 4U;
          red += image.pixels[offset];
          green += image.pixels[offset + 1];
          blue += image.pixels[offset + 2];
          ++count;
        }
      }
      const std::size_t target = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(x)) * 4U;
      small[target] = static_cast<std::uint8_t>(red / std::max(1U, count));
      small[target + 1] = static_cast<std::uint8_t>(green / std::max(1U, count));
      small[target + 2] = static_cast<std::uint8_t>(blue / std::max(1U, count));
      small[target + 3] = 255;
    }
  }
  return textureFromImage(renderer, small, width, height);
}

std::string writeTemplateIfMissing(const fs::path &path) {
  std::error_code code;
  if (fs::exists(path, code))
    return {};
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return "Cannot write " + paths::toUtf8(path);
  output << library::coverNamesTemplate();
  return {};
}

}  // namespace

CoverCache::~CoverCache() { shutdown(); }

void CoverCache::initialize(std::string namesPath,
                            std::vector<std::string> coverRoots) {
  shutdown();
  namesPath_ = std::move(namesPath);
  coverRoots_ = std::move(coverRoots);
  // A killed session can leave a half-written download beside a ROM; it can
  // never be displayed, so drop those before anything looks for art.
  for (const std::string &root : coverRoots_) {
    std::error_code code;
    for (fs::recursive_directory_iterator it(paths::fromUtf8(root), code), end;
         !code && it != end; it.increment(code)) {
      if (it->path().extension() == ".part")
        fs::remove(it->path(), code);
    }
  }
  const fs::path namesFile = paths::fromUtf8(namesPath_);
  std::error_code code;
  if (!namesFile.parent_path().empty())
    fs::create_directories(namesFile.parent_path(), code);
  const std::string writeError = writeTemplateIfMissing(namesFile);
  if (!writeError.empty())
    std::fprintf(stderr, "covers: %s\n", writeError.c_str());

  std::ifstream input(namesFile, std::ios::binary);
  if (!input) {
    std::fprintf(stderr, "covers: cannot read %s\n", paths::toUtf8(namesFile).c_str());
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  std::string error;
  if (!library::parseCoverNames(text, names_, error))
    std::fprintf(stderr, "covers: %s\n", error.c_str());
}

void CoverCache::shutdown() {
  // A download can sit on a dead socket for minutes. Letting that worker
  // outlive the cache is safe: it touches only its own record and the files,
  // and the partial file it leaves is swept on the next launch.
  if (job_ != nullptr && !job_->done.load())
    worker_.detach();
  else if (worker_.joinable())
    worker_.join();
  job_.reset();
  pendingGame_ = {};
  for (auto &entry : covers_)
    SDL_DestroyTexture(entry.second);
  covers_.clear();
  for (auto &entry : wallpapers_)
    SDL_DestroyTexture(entry.second);
  wallpapers_.clear();
  attempted_.clear();
  coverPaths_.clear();
  coverRoots_.clear();
  namesPath_.clear();
  names_.clear();
}

std::string CoverCache::existingCoverPath(const library::GameEntry &game) {
  const auto known = coverPaths_.find(game.id);
  if (known != coverPaths_.end())
    return known->second;
  std::string found;
  // The stock launcher keeps one folder per system, named the way it names the
  // folders under Roms/, with the art named after the game.
  const std::string system(game::platformDescriptor(game.platform).cardDirectory);
  if (!system.empty() && !game.title.empty()) {
    const std::string name = library::coverFileName(game.title);
    for (const std::string &root : coverRoots_) {
      for (const char *extension : {".png", ".jpg"}) {
        const fs::path candidate = paths::fromUtf8(root) / system / (name + extension);
        std::error_code code;
        if (fs::is_regular_file(candidate, code) && !code) {
          found = paths::toUtf8(candidate);
          break;
        }
      }
      if (!found.empty())
        break;
    }
  }
  coverPaths_[game.id] = found;
  return found;
}

std::string CoverCache::coverTarget(const library::GameEntry &game) const {
  if (coverRoots_.empty())
    return {};
  const std::string system(game::platformDescriptor(game.platform).cardDirectory);
  for (const std::string &root : coverRoots_) {
    std::error_code code;
    if (fs::is_directory(paths::fromUtf8(root) / system, code) && !code)
      return library::coverTargetPath(root, game.platform, game.title);
  }
  return library::coverTargetPath(coverRoots_.front(), game.platform, game.title);
}

SDL_Texture *CoverCache::cover(SDL_Renderer *renderer, const library::GameEntry &game) {
  const auto cached = covers_.find(game.id);
  if (cached != covers_.end())
    return cached->second;
  // Art already on the card beats anything a download could bring, and it is
  // the very file the stock launcher shows for the same ROM.
  const std::string path = existingCoverPath(game);
  if (!path.empty())
    return loadCover(renderer, game.id, path);
  // One download at a time. A card that asks while another download is running
  // stays unmarked on purpose: it gets its turn on a later frame instead of
  // being written off for the rest of the session.
  if (job_ != nullptr || !pendingGame_.id.empty())
    return nullptr;
  if (!attempted_.insert(game.id).second)
    return nullptr;
  pendingGame_ = game;
  return nullptr;
}

SDL_Texture *CoverCache::wallpaper(SDL_Renderer *renderer, const library::GameEntry &game) {
  const auto cached = wallpapers_.find(game.id);
  if (cached != wallpapers_.end())
    return cached->second;
  const std::string path = existingCoverPath(game);
  if (!path.empty())
    return loadWallpaper(renderer, game.id, path);
  return nullptr;
}

SDL_Texture *CoverCache::loadCover(SDL_Renderer *renderer, const std::string &id,
                                     const std::string &path) {
  io::Image image;
  std::string error;
  if (!io::loadImage(path, image, error)) {
    std::fprintf(stderr, "covers: %s\n", error.c_str());
    covers_[id] = nullptr;
    return nullptr;
  }
  SDL_Texture *texture = createCoverTexture(renderer, image, true);
  covers_[id] = texture;
  return texture;
}

SDL_Texture *CoverCache::loadWallpaper(SDL_Renderer *renderer, const std::string &id,
                                         const std::string &path) {
  io::Image image;
  std::string error;
  if (!io::loadImage(path, image, error)) {
    wallpapers_[id] = nullptr;
    return nullptr;
  }
  SDL_Texture *texture = createWallpaperTexture(renderer, image);
  wallpapers_[id] = texture;
  return texture;
}

void CoverCache::startDownload(const library::GameEntry &game) {
  const std::vector<std::string> urls = library::coverUrls(game.platform, game.title, names_);
  if (urls.empty())
    return;
  if (worker_.joinable())
    worker_.join();
  const std::string target = coverTarget(game);
  if (target.empty())
    return;
  auto job = std::make_shared<DownloadJob>();
  job->id = game.id;
  job->target = target;
  job_ = job;
  worker_ = std::thread([job, urls, target]() {
    // The system folder may not exist yet on a card that never held art.
    std::error_code made;
    fs::create_directories(paths::fromUtf8(target).parent_path(), made);
    const std::string partial = target + ".part";
    bool ok = false;
    for (const std::string &url : urls) {
      std::uint64_t bytes = 0;
      std::string error;
      if (!io::downloadFile(url, partial, kMaximumCoverBytes, bytes, error)) {
        // A refused or missing file still leaves an empty download behind.
        std::error_code code;
        fs::remove(paths::fromUtf8(partial), code);
        continue;
      }
      // The probe closes before the rename: Windows refuses to replace a file
      // that is still open, which would leave the cover as a .part forever.
      std::vector<std::uint8_t> head(8, 0);
      {
        std::ifstream input(paths::fromUtf8(partial), std::ios::binary);
        input.read(reinterpret_cast<char *>(head.data()),
                   static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(input.gcount()));
      }
      std::error_code code;
      if (bytes != 0 && looksLikeImage(head)) {
        fs::remove(paths::fromUtf8(target), code);
        fs::rename(paths::fromUtf8(partial), paths::fromUtf8(target), code);
        if (!code) {
          ok = true;
          break;
        }
      }
      fs::remove(paths::fromUtf8(partial), code);
    }
    if (!ok) {
      std::error_code code;
      fs::remove(paths::fromUtf8(partial), code);
    }
    job->ok = ok;
    job->done.store(true, std::memory_order_release);
  });
}

void CoverCache::poll(SDL_Renderer *renderer) {
  // Between frames is the only place a download may begin.
  if (job_ == nullptr && !pendingGame_.id.empty()) {
    const library::GameEntry next = pendingGame_;
    pendingGame_ = {};
    startDownload(next);
  }
  if (job_ == nullptr || !job_->done.load(std::memory_order_acquire))
    return;
  if (worker_.joinable())
    worker_.join();
  const bool ok = job_->ok;
  const std::string id = job_->id;
  const std::string target = job_->target;
  job_.reset();
  if (!ok)
    return;
  // The file is on the card now, so the next frame finds it like any other.
  // A miss was remembered as an empty path and has to be forgotten.
  coverPaths_.erase(id);
  if (covers_.find(id) == covers_.end())
    (void)loadCover(renderer, id, target);
}

}  // namespace launcher::ui
