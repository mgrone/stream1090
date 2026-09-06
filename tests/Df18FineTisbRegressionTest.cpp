/* SPDX-License-Identifier: GPL-3.0-or-later */

// REGRESSION (fixed in production code).
//
// handleExtSquitterLongMessage() used to apply Plausibility::checkDF17() to
// every extended squitter reaching the "first sighting" branch, DF18
// included. On DF18, the three bits at the same position as DF17's CA
// (transponder capability) field are CF (Control Field), a different field
// with different values -- see readsb's mode_s.c, decodeExtendedSquitter(),
// "Check CF on DF18" (around line 1461): CF 0/1 are ADS-B (ICAO/anonymous
// address), CF 2 is "Fine TIS-B Message" carrying a genuine 24-bit ICAO
// address when IMF=0, CF 3 is coarse TIS-B, CF 5 is TIS-B with a non-ICAO
// address, CF 6 is ADS-B rebroadcast. checkDF17() rejects values 1-3
// because, for DF17, CA 1-3 mean "no ADS-B capability" -- a check that does
// not apply to DF18's CF at all. A legitimate, common CF=2 Fine TIS-B
// message was therefore rejected on its first sighting, before it ever
// reached the trust-candidate table: it could never earn trust, no matter
// how many times it repeated.
//
// Fixed by scoping checkDF17() to frames that are genuinely DF17-shaped
// (native DF17, or DF19 promoted to 17) and skipping it for DF18, which
// goes through checkICAO() alone like any other first sighting.
//
// Residual, unrelated limitation this test does not cover: DF18 CF=1 and
// CF=5 reports, and CF=2/3 with IMF=1, carry a non-ICAO address (anonymous,
// or a 12-bit Mode-A code plus track-file number), which stream1090's
// ICAO-keyed cache does not distinguish from an ordinary ICAO address. That
// is unchanged by this fix, in either direction.
//
// This test drives the fix through the public bit-level entry point
// (DemodCore::shiftInNewBits) only -- no access to DemodCore/ICAOTable
// internals -- and expresses the CORRECT behavior: a repeated, genuinely
// valid DF18 CF=2 report must be able to earn trust and be emitted
// unchanged, exactly like an equivalent DF17 report (checked here as a
// positive control on the very same harness, to isolate the bug to DF18).
//
// Passes on 16075b9 (initial reference, no Plausibility.hpp yet) and on
// this branch since the fix. Expected to fail on edf006a (upstream,
// unfixed; DF18 case only -- the DF17 positive control still passes
// there).

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
// ("airborne position") with varying CPR bits, matching the layout every
// other test in this suite uses for DF17. df=18 with cf=2 is a Fine TIS-B
// position report using a real ICAO address; df=17 with ca=5 is the
// equivalent genuine ADS-B report, used below as the positive control.
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

// Sends three genuinely-varying reports of the same address, each more than
// 100 us and less than 2 s apart (well inside confirmTrustCandidate's
// [100, 30'000'000]-tick window), and returns every long frame the demod
// emitted along the way. The CPR longitude is nudged each time so no two
// transmissions are bit-identical -- phase deduplication compares raw
// frames, and identical repeats would hide whether the *address* ever
// earned trust behind an unrelated dedup no-op.
std::vector<Bits128> sendThreeReports(uint8_t df, uint8_t cfOrCa) {
    CollectHandler handler;
    DemodCore<1, CollectHandler> demod(handler);
    feedQuiet(demod, 1000); // warm-up

    const auto m1 = makeExtSquitterPosition(df, cfOrCa, Address, 93000, 51372, false);
    const auto m2 = makeExtSquitterPosition(df, cfOrCa, Address, 93000, 51373, false);
    const auto m3 = makeExtSquitterPosition(df, cfOrCa, Address, 93000, 51374, false);

    // fixture sanity: every constructed frame really is CRC-clean, and the
    // three are pairwise distinct, before any of this is handed to DemodCore
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
    // must earn trust and be emitted unchanged by the second and third
    // sighting. If this fails, the harness itself is broken, not the DF18
    // path under test.
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

    // The actual regression: DF18 CF=2 (Fine TIS-B, ICAO address) must
    // behave identically to the DF17 control above. checkDF17() misreading
    // CF as CA rejects it on the very first sighting, so it never reaches
    // the trust-candidate table and is never emitted, no matter how many
    // times it repeats.
    {
        const auto emitted = sendThreeReports(18, 2);
        const auto expected2 = makeExtSquitterPosition(18, 2, Address, 93000, 51373, false);
        const auto expected3 = makeExtSquitterPosition(18, 2, Address, 93000, 51374, false);
        if (emitted.size() != 2 || emitted[0] != expected2 || emitted[1] != expected3) {
            std::printf("DF18 CF=2 (Fine TIS-B) never earned trust: expected "
                "the second and third report emitted unchanged, got %zu long "
                "frame(s) -- Plausibility::checkDF17() misapplies DF17's CA "
                "check (rejects 1-3) to DF18's CF field (2 = Fine TIS-B, a "
                "valid, common value) on the first-sighting insert\n",
                emitted.size());
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
