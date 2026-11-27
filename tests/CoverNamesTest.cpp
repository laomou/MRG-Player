#include <cstdio>
#include <string>
#include <vector>

#include "library/CoverNames.hpp"

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (condition)
    return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

}  // namespace

int main() {
  using launcher::library::CoverNames;
  using launcher::library::coverFileName;
  using launcher::library::coverNameCandidates;
  using launcher::library::coverRepository;
  using launcher::library::coverTargetPath;
  using launcher::library::coverUrls;
  using launcher::library::parseCoverNames;
  using launcher::library::urlEncode;

  CoverNames names;
  std::string error;
  const std::string text =
      "{\n"
      "  \"F1赛车\": \"F-1 Race (Japan)\",\n"
      "  \"超级马里奥\": \"Super Mario Bros. (World)\"\n"
      "}\n";
  check(parseCoverNames(text, names, error), "A flat object of strings parses");
  check(names.size() == 2, "Both entries were stored");
  check(error.empty(), "A good file reports no error");
  check(names["F1赛车"] == "F-1 Race (Japan)", "The mapped name is kept verbatim");

  CoverNames broken;
  check(!parseCoverNames("{\"a\": \"b\",}", broken, error),
        "A trailing comma is rejected");
  check(!error.empty(), "A broken file explains itself");
  check(!parseCoverNames("[1, 2]", broken, error), "A JSON array is rejected");

  CoverNames escaped;
  check(parseCoverNames("{\"a\": \"\\u4f60\\u597d\"}", escaped, error),
        "A unicode escape parses");
  check(escaped["a"] == "你好", "A unicode escape decodes to UTF-8");

  check(coverRepository(launcher::game::GamePlatform::Nes) ==
            "Nintendo_-_Nintendo_Entertainment_System",
        "NES maps to its thumbnail repository");
  check(coverRepository(launcher::game::GamePlatform::Gba) ==
            "Nintendo_-_Game_Boy_Advance",
        "GBA maps to its thumbnail repository");
  check(coverRepository(launcher::game::GamePlatform::Unknown).empty(),
        "An unknown platform has no repository");

  const std::vector<std::string> candidates = coverNameCandidates("F1赛车", names);
  check(!candidates.empty() && candidates.front() == "F-1 Race (Japan)",
        "The mapped name is tried first");
  bool sawTitle = false;
  bool sawWorld = false;
  for (const std::string &candidate : candidates) {
    sawTitle = sawTitle || candidate == "F1赛车";
    sawWorld = sawWorld || candidate == "F1赛车 (World)";
  }
  check(sawTitle, "The title itself is tried");
  check(sawWorld, "A region suffix is tried");

  const std::vector<std::string> urls =
      coverUrls(launcher::game::GamePlatform::Nes, "F1赛车", names);
  check(!urls.empty(), "A NES title produces a URL");
  check(urls.front() ==
            "https://raw.githubusercontent.com/libretro-thumbnails/"
            "Nintendo_-_Nintendo_Entertainment_System/master/Named_Boxarts/"
            "F-1%20Race%20%28Japan%29.png",
        "The first URL points at the mapped No-Intro name");

  CoverNames mirror;
  mirror["本地"] = "https://example.invalid/cover.png";
  const std::vector<std::string> mirrored =
      coverUrls(launcher::game::GamePlatform::Unknown, "本地", mirror);
  check(mirrored.size() == 1 && mirrored.front() == "https://example.invalid/cover.png",
        "A mapped URL is used verbatim, even without a repository");

  check(urlEncode("a b") == "a%20b", "Spaces are percent-encoded");
  check(urlEncode("a/b") == "a%2Fb", "Slashes are percent-encoded");
  const std::string chinese = urlEncode("超级马里奥");
  check(chinese.rfind("%E8%B6%85", 0) == 0, "Chinese text is percent-encoded as UTF-8");

  // The file name on the card has to survive FAT and Windows, because the stock
  // launcher looks for the very same name.
  check(coverFileName("F1赛车") == "F1赛车", "A plain title is kept as is");
  check(coverFileName("a/b:c*?") == "a_b_c__", "Characters a card rejects become underscores");
  check(coverFileName("name. ") == "name", "Trailing dots and spaces are dropped");
  check(coverFileName("///") == "___", "A title with nothing usable still names a file");
  check(coverTargetPath("/mnt/SDCARD/Imgs", launcher::game::GamePlatform::Nes, "F1赛车") ==
            "/mnt/SDCARD/Imgs/FC/F1赛车.png",
        "A cover lands in the system folder the stock launcher reads");
  check(coverTargetPath("/mnt/SDCARD/Imgs/", launcher::game::GamePlatform::Genesis, "Sonic") ==
            "/mnt/SDCARD/Imgs/MD/Sonic.png",
        "A trailing separator does not double up");
  check(coverTargetPath("/mnt/SDCARD/Imgs", launcher::game::GamePlatform::Unknown, "x").empty(),
        "A platform without a card folder has no cover path");
  check(coverTargetPath("", launcher::game::GamePlatform::Nes, "x").empty(),
        "No image folder means nowhere to write");

  if (failures == 0)
    std::printf("cover name checks passed\n");
  return failures == 0 ? 0 : 1;
}
