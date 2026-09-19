/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "DemodCore.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

constexpr int NumStreams = 3;

struct CountingHandler {
  void handleShort(uint64_t, uint64_t) { ++shortCount; }
  void handleLong(uint64_t, const Bits128 &) { ++longCount; }

  uint32_t shortCount{0};
  uint32_t longCount{0};
};

uint64_t makeDf11(uint32_t icao, uint8_t capability = 5) {
  uint64_t frame = (uint64_t(11) << 51) | (uint64_t(capability) << 48)
      | (uint64_t(icao) << 24);
  return frame | CRC::compute<56>(Bits128(frame));
}

uint64_t makeDf5(uint32_t icao, uint16_t squawk = 0x1fff) {
  uint64_t frame = (uint64_t(5) << 51) | (uint64_t(squawk) << 24);
  return frame | (CRC::compute<56>(Bits128(frame)) ^ icao);
}

Bits128 makeDf17(uint32_t icao, uint8_t capability = 5) {
  Bits128 frame((uint64_t(17) << 43) | (uint64_t(capability) << 40)
                    | (uint64_t(icao) << 16),
                0);
  frame.low() |= CRC::compute<112>(frame);
  return frame;
}

void feedQuiet(DemodCore<NumStreams, CountingHandler> &demod, int bits) {
  std::array<uint32_t, NumStreams> input{};
  for (int i = 0; i < bits; ++i)
    demod.shiftInNewBits(input.data());
}

void feedShortsTogether(DemodCore<NumStreams, CountingHandler> &demod,
                        const std::array<uint64_t, NumStreams> &frames) {
  for (int bit = 55; bit >= 0; --bit) {
    std::array<uint32_t, NumStreams> input{};
    for (int stream = 0; stream < NumStreams; ++stream)
      input[stream] = uint32_t((frames[stream] >> bit) & 1u);
    demod.shiftInNewBits(input.data());
  }
}

void feedLongsTogether(DemodCore<NumStreams, CountingHandler> &demod,
                       const std::array<Bits128, NumStreams> &frames) {
  for (int bit = 111; bit >= 0; --bit) {
    std::array<uint32_t, NumStreams> input{};
    for (int stream = 0; stream < NumStreams; ++stream)
      input[stream] = uint32_t(frames[stream][uint8_t(bit)]);
    demod.shiftInNewBits(input.data());
  }
}

bool closeDf11PhaseDetectionsStayUntrusted() {
  constexpr uint32_t addressA = 0x4ca2d1;
  constexpr uint32_t addressB = 0x3e8570;
  CountingHandler handler;
  DemodCore<NumStreams, CountingHandler> demod(handler);
  feedQuiet(demod, 1000);

  const auto a = makeDf11(addressA);
  feedShortsTogether(demod, {a, makeDf11(addressB), a});
  feedQuiet(demod, 200);
  const auto a5 = makeDf5(addressA);
  feedShortsTogether(demod, {a5, makeDf5(addressB), makeDf5(1)});
  feedQuiet(demod, 100);
  feedShortsTogether(demod, {a5, makeDf5(addressB), makeDf5(2)});
  return handler.shortCount == 0;
}

bool closeDf17PhaseDetectionsStayUntrusted() {
  constexpr uint32_t addressA = 0x51c08a;
  constexpr uint32_t addressB = 0x406b21;
  CountingHandler handler;
  DemodCore<NumStreams, CountingHandler> demod(handler);
  feedQuiet(demod, 1000);

  const auto a = makeDf17(addressA);
  feedLongsTogether(demod, {a, makeDf17(addressB), a});
  feedQuiet(demod, 200);
  const auto a5 = makeDf5(addressA);
  feedShortsTogether(demod, {a5, makeDf5(addressB), makeDf5(1)});
  feedQuiet(demod, 100);
  feedShortsTogether(demod, {a5, makeDf5(addressB), makeDf5(2)});
  return handler.longCount == 0 && handler.shortCount == 0;
}

bool zeroIcaoIsNeverEmitted() {
  CountingHandler handler;
  DemodCore<NumStreams, CountingHandler> demod(handler);
  feedQuiet(demod, 1000);

  const auto zero11 = makeDf11(0);
  feedShortsTogether(demod, {zero11, makeDf11(1), zero11});
  feedQuiet(demod, 100);
  feedShortsTogether(demod, {makeDf11(0, 5), makeDf11(2), makeDf11(0, 5)});
  feedQuiet(demod, 100);
  const auto zero17 = makeDf17(0);
  feedLongsTogether(demod, {zero17, makeDf17(3), zero17});
  feedQuiet(demod, 100);
  feedLongsTogether(demod, {makeDf17(0, 5), makeDf17(4), makeDf17(0, 5)});

  return handler.shortCount == 0 && handler.longCount == 0;
}

} // namespace

int main() {
  int failures = 0;
  if (!closeDf11PhaseDetectionsStayUntrusted()) {
    std::printf("DF11 phase detections bypassed minimum separation\n");
    ++failures;
  }
  if (!closeDf17PhaseDetectionsStayUntrusted()) {
    std::printf("DF17 phase detections bypassed minimum separation\n");
    ++failures;
  }
  if (!zeroIcaoIsNeverEmitted()) {
    std::printf("ICAO zero was emitted\n");
    ++failures;
  }
  return failures != 0;
}
