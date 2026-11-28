#include "util/UpdateChecker.hpp"

#include "util/Download.hpp"
#include "util/Utf8Path.hpp"
#include "util/Version.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace launcher::util {
namespace {
namespace fs = std::filesystem;

// The latest-release endpoint skips pre-releases, and this project currently
// publishes one, so ask for the short list and take the highest tag in it.
constexpr const char *kReleasesUrl =
    "https://api.github.com/repos/laomou/MRG-Player/releases?per_page=10";
constexpr std::uint64_t kMaximumResponseBytes = 256 * 1024;

std::vector<std::string> releaseTags(const std::string &json) {
  std::vector<std::string> tags;
  const std::string key = "\"tag_name\"";
  std::size_t at = 0;
  while ((at = json.find(key, at)) != std::string::npos) {
    at += key.size();
    at = json.find(':', at);
    if (at == std::string::npos)
      break;
    at = json.find('"', at);
    if (at == std::string::npos)
      break;
    const std::size_t begin = at + 1;
    const std::size_t end = json.find('"', begin);
    if (end == std::string::npos)
      break;
    tags.push_back(json.substr(begin, end - begin));
    at = end + 1;
  }
  return tags;
}

std::string readTextFile(const std::string &path) {
  std::ifstream file(fs::path(paths::fromUtf8(path)), std::ios::binary);
  if (!file)
    return {};
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

}  // namespace

struct UpdateChecker::State {
  std::mutex mutex;
  UpdateResult result;
  std::atomic<bool> done{false};
};

bool UpdateChecker::start(std::string currentVersion, std::string tempPath,
                          std::string caBundle) {
  if (busy())
    return false;
  auto state = std::make_shared<State>();
  state_ = state;
  std::thread([state, currentVersion = std::move(currentVersion),
               tempPath = std::move(tempPath),
               caBundle = std::move(caBundle)]() {
    std::string error;
    std::uint64_t written = 0;
    UpdateResult result;
    result.status = UpdateStatus::Failed;
    if (io::downloadFile(kReleasesUrl, tempPath, kMaximumResponseBytes, written,
                         error, caBundle) != io::DownloadStatus::Ok) {
      std::error_code ignored;
      fs::remove(fs::path(paths::fromUtf8(tempPath)), ignored);
    } else {
      const std::string json = readTextFile(tempPath);
      std::error_code ignored;
      fs::remove(fs::path(paths::fromUtf8(tempPath)), ignored);
      const std::vector<std::string> tags = releaseTags(json);
      std::string newest;
      for (const std::string &tag : tags) {
        if (newest.empty() || compareVersions(tag, newest) > 0)
          newest = tag;
      }
      if (!newest.empty()) {
        if (compareVersions(newest, currentVersion) > 0) {
          result.status = UpdateStatus::Available;
          result.version = displayVersion(newest);
        } else {
          result.status = UpdateStatus::UpToDate;
        }
      }
    }
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->result = std::move(result);
    }
    state->done.store(true, std::memory_order_release);
  }).detach();
  return true;
}

bool UpdateChecker::busy() const {
  return state_ != nullptr && !state_->done.load(std::memory_order_acquire);
}

bool UpdateChecker::poll(UpdateResult &result) {
  if (state_ == nullptr || !state_->done.load(std::memory_order_acquire))
    return false;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    result = state_->result;
  }
  state_.reset();
  return true;
}

} // namespace launcher::util
