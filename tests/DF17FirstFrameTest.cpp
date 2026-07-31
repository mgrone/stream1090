/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "DemodCore.hpp"

#include <cstdint>

namespace {

struct CapturingHandler {
    void handleShort(uint64_t, uint64_t) {}

    void handleLong(uint64_t, const Bits128& frame) {
        longCount++;
        lastLong = frame;
    }

    uint32_t longCount { 0 };
    Bits128 lastLong;
};

Bits128 makeDF17(uint32_t icao) {
    constexpr uint8_t capability = 5;
    const uint64_t high = (uint64_t(17) << 43)
        | (uint64_t(capability) << 40)
        | (uint64_t(icao) << 16);
    Bits128 frame(high, 0);
    frame.low() = CRC::compute<112>(frame);
    return frame;
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
    const auto frame = makeDF17(0xabcdef);
    if (CRC::compute<112>(frame) != 0)
        return 1;

    CapturingHandler handler;
    DemodCore<1, CapturingHandler> demod(handler);
    feedFrame(demod, frame);

    return !(handler.longCount == 1 && handler.lastLong == frame);
}
