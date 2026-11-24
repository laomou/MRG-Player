// Known-answer tests for the SHA-256 used to pin downloaded core archives.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "util/Sha256.hpp"

namespace {

int failures = 0;

void expect(const std::string &actual, const std::string &expected, const char *what) {
  if (actual == expected)
    return;
  std::fprintf(stderr, "FAIL: %s\n  expected %s\n  actual   %s\n", what, expected.c_str(), actual.c_str());
  ++failures;
}

} // namespace

int main() {
  expect(launcher::util::sha256Hex(nullptr, 0),
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty input");
  const std::string abc = "abc";
  expect(launcher::util::sha256Hex(reinterpret_cast<const std::uint8_t *>(abc.data()), abc.size()),
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
  const std::string longText = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  expect(launcher::util::sha256Hex(reinterpret_cast<const std::uint8_t *>(longText.data()), longText.size()),
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "two-block message");
  std::vector<std::uint8_t> block(64, 'a');
  expect(launcher::util::sha256Hex(block.data(), block.size()),
         "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", "single block");
  if (failures != 0) {
    std::fprintf(stderr, "%d sha256 checks failed\n", failures);
    return 1;
  }
  std::printf("sha256 checks passed\n");
  return 0;
}
