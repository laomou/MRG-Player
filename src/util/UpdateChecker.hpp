#pragma once

#include <memory>
#include <string>

namespace launcher::util {

enum class UpdateStatus {
  Idle,
  Checking,
  UpToDate,
  Available,
  Failed,
};

struct UpdateResult {
  UpdateStatus status = UpdateStatus::Failed;
  std::string version;  // Filled when status is Available.
};

// Answers "is there a newer release?" without downloading it. The request runs
// on a detached worker so a slow or unreachable host cannot freeze the menu;
// the owner polls once a frame. The temporary file is removed by the worker.
class UpdateChecker {
public:
  UpdateChecker() = default;
  ~UpdateChecker() = default;
  UpdateChecker(const UpdateChecker &) = delete;
  UpdateChecker &operator=(const UpdateChecker &) = delete;

  // False when a check is already running. tempPath is where the small JSON
  // response is written; caBundle is optional and only used by curl/wget.
  bool start(std::string currentVersion, std::string tempPath,
             std::string caBundle = {});

  [[nodiscard]] bool busy() const;

  // True once, when the worker has finished. Resets the checker so the next
  // press can start again.
  bool poll(UpdateResult &result);

private:
  struct State;
  std::shared_ptr<State> state_;
};

} // namespace launcher::util
