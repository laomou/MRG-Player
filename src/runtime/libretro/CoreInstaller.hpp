#pragma once

#include <string>
#include <vector>

#include "game/GamePlatform.hpp"

namespace launcher::runtime::libretro {

// One core the launcher can fetch. The libretro buildbot only publishes a
// rolling "latest" build per platform, so a package is a name rather than a
// pinned archive: an install downloads whatever that directory holds today.
struct CorePackage {
  game::GamePlatform platform;
  const char *core;
};

// Packages for this build's platform; empty when the buildbot publishes no
// cores for it.
[[nodiscard]] const std::vector<CorePackage> &corePackages();
[[nodiscard]] std::string coreArchiveUrl(const CorePackage &package);
[[nodiscard]] std::string coreLibraryName(const CorePackage &package);
// Base URL the archives are fetched from. MRG_CORES_BASE_URL overrides it, so a
// mirror, a local directory served over HTTP, or a test can stand in.
[[nodiscard]] std::string coreArchiveBaseUrl();

struct InstallOutcome {
  bool ok = false;
  std::string message;
};

// Blocking: downloads and unpacks one core into coresDirectory.
[[nodiscard]] InstallOutcome installCore(const CorePackage &package, const std::string &coresDirectory);

} // namespace launcher::runtime::libretro
