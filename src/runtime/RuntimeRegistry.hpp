#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "game/GamePlatform.hpp"

namespace launcher::library {
struct GameEntry;
}
namespace launcher::runtime {

class IGameRuntime;

enum class SaveDataStatus { Present, Missing, Removed, Failed, Unsupported };

class RuntimeRegistry final {
public:
  [[nodiscard]] bool supports(game::GamePlatform platform) const noexcept;
  [[nodiscard]] std::unique_ptr<IGameRuntime>
  create(game::GamePlatform platform) const;
  [[nodiscard]] SaveDataStatus
  inspectSaveData(const library::GameEntry &game, std::string &error,
                  std::string_view directoryOverride = {}) const;
  [[nodiscard]] SaveDataStatus
  deleteSaveData(const library::GameEntry &game, std::string &error,
                 std::string_view directoryOverride = {}) const;
};

} // namespace launcher::runtime
