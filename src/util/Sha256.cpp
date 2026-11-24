#include "util/Sha256.hpp"

#include <array>
#include <fstream>
#include <vector>

namespace launcher::util {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

std::uint32_t rotateRight(std::uint32_t value, int bits) {
  return (value >> bits) | (value << (32 - bits));
}

void compress(std::array<std::uint32_t, 8> &state, const std::uint8_t *block) {
  std::uint32_t words[64];
  for (int index = 0; index < 16; ++index) {
    words[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(block[index * 4 + 3]);
  }
  for (int index = 16; index < 64; ++index) {
    const std::uint32_t first = words[index - 15];
    const std::uint32_t second = words[index - 2];
    const std::uint32_t sigma0 = rotateRight(first, 7) ^ rotateRight(first, 18) ^ (first >> 3);
    const std::uint32_t sigma1 = rotateRight(second, 17) ^ rotateRight(second, 19) ^ (second >> 10);
    words[index] = words[index - 16] + sigma0 + words[index - 7] + sigma1;
  }
  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];
  std::uint32_t e = state[4];
  std::uint32_t f = state[5];
  std::uint32_t g = state[6];
  std::uint32_t h = state[7];
  for (int index = 0; index < 64; ++index) {
    const std::uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
    const std::uint32_t choice = (e & f) ^ (~e & g);
    const std::uint32_t temp1 = h + sum1 + choice + kRoundConstants[index] + words[index];
    const std::uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

std::string toHex(const std::array<std::uint32_t, 8> &state) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(64);
  for (std::uint32_t word : state) {
    for (int shift = 28; shift >= 0; shift -= 4)
      hex.push_back(kDigits[(word >> shift) & 0x0FU]);
  }
  return hex;
}

} // namespace

std::string sha256Hex(const std::uint8_t *data, std::size_t size) {
  std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                     0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::size_t offset = 0;
  for (; size - offset >= 64; offset += 64)
    compress(state, data + offset);

  // The tail is padded with 0x80, zeros, and the 64-bit length in bits.
  std::vector<std::uint8_t> tail;
  tail.assign(data + offset, data + size);
  tail.push_back(0x80);
  while ((tail.size() % 64) != 56)
    tail.push_back(0);
  const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8U;
  for (int shift = 56; shift >= 0; shift -= 8)
    tail.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xFFU));
  for (std::size_t index = 0; index < tail.size(); index += 64)
    compress(state, tail.data() + index);
  return toHex(state);
}

std::string sha256File(const std::string &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return {};
  const std::streamoff size = input.tellg();
  if (size < 0)
    return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (size > 0 &&
      !input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size)))
    return {};
  return sha256Hex(bytes.data(), bytes.size());
}

} // namespace launcher::util
