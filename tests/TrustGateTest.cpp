/* SPDX-License-Identifier: GPL-3.0-or-later */

// Trust gate regression, driven at the bit level: DemodCore is fed the
// synthetic bit stream of three aircraft directly, no RF involved. The
// noise regression covers the RF path; this one covers the trust logic.
//
//  X  two clean DF11 all-call replies (CRC 0) 600 us apart, then two DF5:
//     the second reply confirms and emits, the DF5s are emitted once the
//     squawk is learned.
//  Y  a single clean DF11, then a DF5: nothing may be emitted, one
//     sighting proves nothing.
//  Z  one clean DF11 followed by two replies with an interrogator overlay
//     (small syndrome), then two DF5 around another overlaid reply: the
//     overlaid pair promotes the existing untrusted entry, so a Mode-S only
//     aircraft works end to end.

#include "DemodCore.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr int NumStreams = 48;

// Mode-S CRC-24, generator 0xFFF409, computed over nbits with the parity
// field already in place: the residue is the claimed address for the
// address-parity formats and 0 for a clean extended squitter.
uint32_t modeSCrc(uint64_t frame, int nbits) {
  uint32_t reg = (frame >> (nbits - 24)) & 0xFFFFFFu;
  for (int i = 0; i < nbits - 24; ++i) {
    const uint32_t bit = (frame >> (nbits - 25 - i)) & 1u;
    reg = (reg & 0x800000u) ? (((reg << 1) | bit) ^ 0xFFF409u)
                            : ((reg << 1) | bit);
    reg &= 0xFFFFFFu;
  }
  return reg;
}

constexpr uint32_t AddressX = 0x4CA2D1;
constexpr uint32_t AddressY = 0x3E8570;
constexpr uint32_t AddressZ = 0x51C08A;

// 32 data bits with the parity field left at zero.
uint64_t df11Data(uint32_t icao) {
  return (0b01011ull << 51) | (5ull << 48) | (uint64_t(icao) << 24);
}

uint64_t df5Data(uint32_t ac) {
  // DF 5, FS 0, DR 0, UM 0, AC 13 bits; AP left at zero: the address is
  // not transmitted, it is recovered from the parity.
  return (0b00101ull << 51) | (uint64_t(ac) << 24);
}

// DF11 all-call reply, CRC 0 (interrogator code II 0).
uint64_t makeDf11(uint32_t icao) {
  const uint64_t data = df11Data(icao);
  return data | modeSCrc(data, 56);
}

// DF11 with the interrogator overlay: residue is the small II code.
uint64_t makeDf11Interrogated(uint32_t icao, uint32_t code) {
  const uint64_t data = df11Data(icao);
  return data | (modeSCrc(data, 56) ^ code);
}

// DF5 surveillance identity reply: residue is the address. The squawk is
// nonzero: checkSquawk refuses squawk 0 outright.
uint64_t makeDf5(uint32_t icao, uint32_t ac = 0x1FFF) {
  const uint64_t data = df5Data(ac);
  return data | (modeSCrc(data, 56) ^ icao);
}

struct EmittedFrame {
  bool isLong;
  uint64_t value;
};

struct CollectHandler {
  void handleShort(uint64_t, uint64_t frame) {
    frames.push_back({false, frame});
  }
  void handleLong(uint64_t, const Bits128 &) { frames.push_back({true, 0}); }

  std::vector<EmittedFrame> frames;
};

// Feeds one bit into every stream: the aircraft is received on all of
// them, and the phase dedup keeps only the first detection.
void feedBit(DemodCore<NumStreams, CollectHandler> &demod, int bit) {
  std::array<uint32_t, NumStreams> cmp{};
  cmp.fill(uint32_t(bit));
  demod.shiftInNewBits(cmp.data());
}

void feedFrame(DemodCore<NumStreams, CollectHandler> &demod, uint64_t frame) {
  for (int i = 0; i < 56; ++i)
    feedBit(demod, int((frame >> (55 - i)) & 1u));
}

void feedQuiet(DemodCore<NumStreams, CollectHandler> &demod, int bits) {
  for (int i = 0; i < bits; ++i)
    feedBit(demod, 0);
}

} // namespace

int main() {
  const uint64_t x11 = makeDf11(AddressX);
  const uint64_t y11 = makeDf11(AddressY);
  const uint64_t x5 = makeDf5(AddressX);
  const uint64_t y5 = makeDf5(AddressY);
  const uint64_t z11a = makeDf11Interrogated(AddressZ, 10);
  // the interrogator overlay is stripped on emission: the crc<80 path
  // sends the frame with the parity corrected to the plain CRC
  const uint64_t z11b = makeDf11(AddressZ);
  const uint64_t z5 = makeDf5(AddressZ);

  CollectHandler handler;
  DemodCore<NumStreams, CollectHandler> demod(handler);

  feedQuiet(demod, 1000); // warm-up
  feedFrame(demod, x11);  // first sighting of X: cached untrusted
  feedQuiet(demod, 300);
  feedFrame(demod, y11);  // first sighting of Y: cached untrusted
  feedQuiet(demod, 300);
  feedFrame(demod, x11);  // second sighting: trusted, emitted
  feedQuiet(demod, 300);
  feedFrame(demod, x5);   // X trusted: squawk learned, not emitted
  feedQuiet(demod, 150);
  feedFrame(demod, x11);  // keeps X alive and resets the phase dedup
  feedQuiet(demod, 150);
  feedFrame(demod, x5);   // squawk matches: emitted
  feedQuiet(demod, 300);
  feedFrame(demod, y5);   // Y is not trusted: must stay silent
  feedQuiet(demod, 300);
  feedFrame(demod, z11b); // clean first sighting of Z: cached untrusted
  feedQuiet(demod, 300);
  feedFrame(demod, z11a); // first sighting of Z: silent
  feedQuiet(demod, 300);
  feedFrame(demod, z11a); // confirmed: trusted, silent
  feedQuiet(demod, 300);
  feedFrame(demod, z5);   // Z trusted: squawk learned, not emitted
  feedQuiet(demod, 150);
  feedFrame(demod, z11a); // keeps Z alive and resets the phase dedup
  feedQuiet(demod, 150);
  feedFrame(demod, z5);   // squawk matches: emitted
  feedQuiet(demod, 1000);

  // DF5 emission needs a second sighting of the same squawk; a clean DF11
  // of a known address emits; the confirming crc<80 sighting stays silent
  // (the contract DF11InterrogatorTest pins) and the next one emits.
  const std::array<uint64_t, 5> expected{
      {x11, x11, x5, z11b, z5}};
  int failures = 0;

  std::vector<uint64_t> emittedShort;
  for (const auto &f : handler.frames) {
    if (f.isLong) {
      std::printf("unexpected long frame emitted\n");
      ++failures;
    } else {
      emittedShort.push_back(f.value);
    }
  }

  std::sort(emittedShort.begin(), emittedShort.end());
  auto expectedSorted = expected;
  std::sort(expectedSorted.begin(), expectedSorted.end());
  if (emittedShort != std::vector<uint64_t>(expectedSorted.begin(),
                                            expectedSorted.end())) {
    std::printf("emitted frames mismatch:\n  expected:");
    for (auto v : expectedSorted)
      std::printf(" %014llX", static_cast<unsigned long long>(v));
    std::printf("\n  emitted: ");
    for (auto v : emittedShort)
      std::printf(" %014llX", static_cast<unsigned long long>(v));
    std::printf("\n");
    ++failures;
  }

  if (failures == 0) {
    std::printf("trust gate: %zu emitted, all as expected\n",
                handler.frames.size());
    return 0;
  }
  std::printf("FAILED with %d problem(s)\n", failures);
  return 1;
}
