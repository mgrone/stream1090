/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "DemodCore.hpp"

#include <array>
#include <cstdint>

namespace {

struct CapturingHandler {
    void handleShort(uint64_t, uint64_t) {}

    void handleLong(uint64_t sampleTime, const Bits128& frame) {
        if (longCount < frames.size()) {
            sampleTimes[longCount] = sampleTime;
            frames[longCount] = frame;
        }
        ++longCount;
    }

    uint32_t longCount { 0 };
    std::array<uint64_t, 2> sampleTimes{};
    std::array<Bits128, 2> frames { Bits128(), Bits128() };
};

Bits128 makeDF17(uint32_t icao, uint8_t typeCode) {
    constexpr uint8_t capability = 5;
    const uint64_t high = (uint64_t(17) << 43)
        | (uint64_t(capability) << 40)
        | (uint64_t(icao) << 16)
        | (uint64_t(typeCode) << 11);
    Bits128 frame(high, 0);
    frame.low() = CRC::compute<112>(frame);
    return frame;
}

void feedSilence(DemodCore<1, CapturingHandler>& demod, uint32_t bits) {
    for (uint32_t bit = 0; bit < bits; ++bit) {
        uint32_t value[] = { 0 };
        demod.shiftInNewBits(value);
    }
}

void feedFrame(DemodCore<1, CapturingHandler>& demod, const Bits128& frame) {
    for (int bit = 111; bit >= 0; --bit) {
        uint32_t value[] = { uint32_t(frame.get(bit)) };
        demod.shiftInNewBits(value);
    }

    for (int bit = 0; bit < 16; ++bit) {
        uint32_t value[] = { 0 };
        demod.shiftInNewBits(value);
    }
}

} // namespace

int main() {
    const auto first = makeDF17(0xabcdef, 1);
    const auto second = makeDF17(0xabcdef, 2);
    const auto unrelated = makeDF17(0x123456, 1);
    if (CRC::compute<112>(first) != 0
            || CRC::compute<112>(second) != 0
            || CRC::compute<112>(unrelated) != 0)
        return 1;

    CapturingHandler handler;
    DemodCore<1, CapturingHandler> demod(handler);
    feedFrame(demod, first);
    if (handler.longCount != 0)
        return 1;

    feedSilence(demod, 128);
    feedFrame(demod, unrelated);
    if (handler.longCount != 0)
        return 1;

    feedSilence(demod, 128);
    feedFrame(demod, second);

    if (!(handler.longCount == 2
        && handler.frames[0] == first
        && handler.frames[1] == second
        && handler.sampleTimes[0] < handler.sampleTimes[1]))
        return 1;

    CapturingHandler expiredHandler;
    DemodCore<1, CapturingHandler> expiredDemod(expiredHandler);
    feedFrame(expiredDemod, first);
    feedSilence(expiredDemod, 2'000'001);
    feedFrame(expiredDemod, second);

    return !(expiredHandler.longCount == 1
        && expiredHandler.frames[0] == second);
}
