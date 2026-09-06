/* SPDX-License-Identifier: GPL-3.0-or-later */

// Regression: handleExtSquitterLongMessage() applies Plausibility::checkDF17()
// to every extended squitter that reaches the first-sighting insert, DF18
// included. DF18 uses the same bit position for CF (Control Field) that
// DF17 uses for CA (transponder capability); checkDF17() rejects CA values
// 1-3, which also rejects CF=2 ("Fine TIS-B Message", a report carrying a
// genuine ICAO address -- see readsb's mode_s.c, decodeExtendedSquitter()).
// A legitimate CF=2 report is therefore rejected on its first sighting and
// can never earn trust, no matter how many times it repeats.
//
// Expected: a repeated DF18 CF=2 report earns trust and is emitted
// unchanged, exactly like an equivalent DF17 report (checked here as a
// positive control on the same harness, to isolate the bug to DF18).
//
// Driven through the public bit-level entry point (DemodCore::shiftInNewBits)
// only.

#include "DemodCore.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t Address = 0x4CA2D1; // passes Plausibility::checkICAO()

struct CollectHandler {
    void handleShort(uint64_t, uint64_t) {}
    void handleLong(uint64_t, const Bits128& frame) { longFrames.push_back(frame); }

    std::vector<Bits128> longFrames;
};

// DF (5 bits) + CF/CA (3 bits) + AA/ICAO (24 bits) + ME typecode 11
// ("airborne position") with varying CPR bits: df=18 cf=2 is Fine TIS-B
// with a real ICAO address, df=17 ca=5 is the equivalent genuine ADS-B
// report used below as the positive control.
Bits128 makeExtSquitterPosition(uint8_t df, uint8_t cfOrCa, uint32_t icao,
        uint32_t latCpr, uint32_t lonCpr, bool odd) {
    const uint64_t high = (uint64_t(df) << 43)
        | (uint64_t(cfOrCa) << 40)
        | (uint64_t(icao) << 16)
        | (uint64_t(11) << 11); // TC 11: airborne position
    const uint64_t low = (uint64_t(odd) << 58)
        | (uint64_t(latCpr) << 41)
        | (uint64_t(lonCpr) << 24);
    Bits128 frame(high, low);
    frame.low() |= CRC::compute<112>(frame);
    return frame;
}

void feedFrame(DemodCore<1, CollectHandler>& demod, const Bits128& frame) {
    for (int bit = 111; bit >= 0; --bit) {
        uint32_t value[] = { uint32_t(frame.get(bit)) };
        demod.shiftInNewBits(value);
    }
}

void feedQuiet(DemodCore<1, CollectHandler>& demod, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        uint32_t value[] = { 0 };
        demod.shiftInNewBits(value);
    }
}

// Sends three varying reports of the same address (CPR longitude nudged
// each time so phase dedup cannot hide whether the address itself earned
// trust), each more than 100 us and less than 2 s apart, and returns every
// long frame emitted along the way.
std::vector<Bits128> sendThreeReports(uint8_t df, uint8_t cfOrCa) {
    CollectHandler handler;
    DemodCore<1, CollectHandler> demod(handler);
    feedQuiet(demod, 1000); // warm-up

    const auto m1 = makeExtSquitterPosition(df, cfOrCa, Address, 93000, 51372, false);
    const auto m2 = makeExtSquitterPosition(df, cfOrCa, Address, 93000, 51373, false);
    const auto m3 = makeExtSquitterPosition(df, cfOrCa, Address, 93000, 51374, false);

    if (CRC::compute<112>(m1) != 0 || CRC::compute<112>(m2) != 0
            || CRC::compute<112>(m3) != 0) {
        std::printf("fixture error: constructed frame is not CRC-clean\n");
        return {};
    }
    if (m1.low() == m2.low() || m2.low() == m3.low()) {
        std::printf("fixture error: successive reports are bit-identical\n");
        return {};
    }

    feedFrame(demod, m1);
    feedQuiet(demod, 300); // > 100 us, < 2 s
    feedFrame(demod, m2);
    feedQuiet(demod, 300);
    feedFrame(demod, m3);
    feedQuiet(demod, 1000);

    return handler.longFrames;
}

} // namespace

int main() {
    int failures = 0;

    // Positive control: an equivalent DF17 report (CA=5, valid capability)
    // earns trust and is emitted unchanged by the second and third
    // sighting. If this fails, the harness is broken, not the DF18 path.
    {
        const auto emitted = sendThreeReports(17, 5);
        const auto expected2 = makeExtSquitterPosition(17, 5, Address, 93000, 51373, false);
        const auto expected3 = makeExtSquitterPosition(17, 5, Address, 93000, 51374, false);
        if (emitted.size() != 2 || emitted[0] != expected2 || emitted[1] != expected3) {
            std::printf("DF17 positive control failed: expected the second "
                "and third report emitted unchanged, got %zu long frame(s)\n",
                emitted.size());
            ++failures;
        }
    }

    // The regression: DF18 CF=2 must behave identically to the DF17
    // control above.
    {
        const auto emitted = sendThreeReports(18, 2);
        const auto expected2 = makeExtSquitterPosition(18, 2, Address, 93000, 51373, false);
        const auto expected3 = makeExtSquitterPosition(18, 2, Address, 93000, 51374, false);
        if (emitted.size() != 2 || emitted[0] != expected2 || emitted[1] != expected3) {
            std::printf("DF18 CF=2 (Fine TIS-B) never earned trust: expected "
                "the second and third report emitted unchanged, got %zu long "
                "frame(s)\n", emitted.size());
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("DF18 Fine TIS-B: earns trust and is emitted like DF17\n");
        return 0;
    }
    std::printf("FAILED with %d problem(s)\n", failures);
    return 1;
}
