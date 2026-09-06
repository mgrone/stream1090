/* SPDX-License-Identifier: GPL-3.0-or-later */

// Regression: handleExtSquitterLongMessage() treats every DF19 candidate as
// a possibly-corrupted DF17: it flips bit 108 (the DF field's
// second-least-significant bit, where 17 = 10001 and 19 = 10011 differ) and
// XORs the CRC by CRC::delta<108>() before the crc==0 check runs. When the
// flip lands on a real single-bit error, the reconstructed frame's CRC
// becomes zero and is treated exactly like a genuinely clean squitter, with
// nothing distinguishing a guessed repair from an actual clean reception.
// Repeating a repair-shaped signal can therefore create trust for an
// address that was never cleanly received.
//
// Three independent cases, driven through the public bit-level entry point
// only:
//   A) a brand-new address, signal shaped so the DF field flips 19->17 --
//      nothing may ever be emitted, and trust must never appear.
//   C) an address with exactly one prior clean DF17 sighting, so it is
//      already in the cache but not yet trusted (a second sighting is what
//      earns trust) -- the same repair-shaped signal must still neither
//      emit nor promote it.
//   B) an address already trusted through clean DF17 sightings, then the
//      same bit-108 error on a message for that address -- recovery is
//      allowed to emit the corrected, original DF17 frame; that is
//      legitimate error correction for an address already vetted, not
//      trust creation.
//
// Case C is the one a fix can get wrong by assuming any cache hit found by
// findWithCA() is already trusted: it is not, until a second sighting (or
// this same repair, if left unguarded) confirms it.
//
// This test does not call into Plausibility:: at all. A dedicated positive
// control (below) confirms with DemodCore's own public behavior that the
// address used in case A is not rejected for some unrelated reason.

#include "DemodCore.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct CollectHandler {
    void handleShort(uint64_t, uint64_t) {}
    void handleLong(uint64_t, const Bits128& frame) { longFrames.push_back(frame); }

    std::vector<Bits128> longFrames;
};

Bits128 makeDf17Position(uint32_t icao, uint32_t latCpr, uint32_t lonCpr, bool odd) {
    const uint64_t high = (uint64_t(17) << 43)
        | (uint64_t(5) << 40)
        | (uint64_t(icao) << 16)
        | (uint64_t(11) << 11);
    const uint64_t low = (uint64_t(odd) << 58)
        | (uint64_t(latCpr) << 41)
        | (uint64_t(lonCpr) << 24);
    Bits128 frame(high, low);
    frame.low() |= CRC::compute<112>(frame);
    return frame;
}

// A genuinely valid DF17 report with bit 108 flipped: the DF field now
// reads 19, and the raw CRC over the received bits is CRC::delta<108>()
// (nonzero) -- the exact signal shape the DF19->17 promotion "recovers"
// back to this same DF17 frame.
Bits128 asDf19WithBit108Error(Bits128 df17) {
    df17.flip(108);
    return df17;
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

uint8_t receivedDf(const Bits128& frame) {
    return uint8_t((frame.high() >> 43) & 0x1F);
}

} // namespace

int main() {
    int failures = 0;

    // --- Case A: brand-new, never-seen address -----------------------
    // 0x1A2B3C: verified below, via a live positive control, to pass
    // whatever address-plausibility screening exists on this reference.
    // Using an address that unrelated screening would itself reject would
    // pass this test for the wrong reason.
    constexpr uint32_t unknownAddress = 0x1A2B3C;
    {
        CollectHandler handler;
        DemodCore<1, CollectHandler> demod(handler);
        feedQuiet(demod, 1000);
        const auto p1 = makeDf17Position(unknownAddress, 20000, 20000, false);
        const auto p2 = makeDf17Position(unknownAddress, 20000, 20001, false);
        feedFrame(demod, p1);
        feedQuiet(demod, 300); // > 100 us, < 2 s
        feedFrame(demod, p2); // second clean sighting: trusted, emitted
        feedQuiet(demod, 300);
        if (handler.longFrames.size() != 1 || handler.longFrames[0] != p2) {
            std::printf("positive control failed: two genuinely clean DF17 "
                "sightings of 0x1A2B3C did not earn trust and get emitted "
                "(%zu long frame(s)) -- pick a different address\n",
                handler.longFrames.size());
            return 1;
        }
    }

    const auto clean1 = makeDf17Position(unknownAddress, 93000, 51372, false);
    const auto clean2 = makeDf17Position(unknownAddress, 93000, 51373, false);
    const auto clean3 = makeDf17Position(unknownAddress, 93000, 51374, false);
    const auto signal1 = asDf19WithBit108Error(clean1);
    const auto signal2 = asDf19WithBit108Error(clean2);
    const auto signal3 = asDf19WithBit108Error(clean3);

    // fixture sanity: each signal presents as DF 19 with the syndrome the
    // bit-108 flip predicts, and reconstructs a genuinely clean DF17 frame.
    auto checkFixture = [](const Bits128& clean, const Bits128& signal) {
        if (CRC::compute<112>(clean) != 0) {
            std::printf("fixture error: clean DF17 frame is not CRC-clean\n");
            return false;
        }
        if (receivedDf(signal) != 19) {
            std::printf("fixture error: bit-108 flip did not turn DF 17 into 19\n");
            return false;
        }
        if (CRC::compute<112>(signal) != CRC::delta<108>()) {
            std::printf("fixture error: flipped frame's syndrome is not "
                "CRC::delta<108>()\n");
            return false;
        }
        return true;
    };
    if (!checkFixture(clean1, signal1) || !checkFixture(clean2, signal2)
            || !checkFixture(clean3, signal3))
        return 1;

    {
        CollectHandler handler;
        DemodCore<1, CollectHandler> demod(handler);
        feedQuiet(demod, 1000);
        feedFrame(demod, signal1);
        feedQuiet(demod, 300); // > 100 us, < 2 s
        feedFrame(demod, signal2);
        feedQuiet(demod, 300);
        feedFrame(demod, signal3);
        feedQuiet(demod, 1000);

        // Frames that require repair must never seed trust for an address
        // that has never been cleanly received. Nothing may be emitted.
        if (!handler.longFrames.empty()) {
            std::printf("Case A (unknown address): %zu frame(s) emitted from "
                "a repeated DF19-shaped signal for an address never cleanly "
                "received -- a repair created trust for a brand-new address\n",
                handler.longFrames.size());
            ++failures;
        }

        // Corroborate independently: if trust had been created, a later
        // genuinely clean DF17 for the same address would be emitted on
        // its very first (post-promotion) sighting instead of needing its
        // own separate two-sighting confirmation.
        const auto probe = makeDf17Position(unknownAddress, 10000, 10000, true);
        feedFrame(demod, probe);
        feedQuiet(demod, 1000);
        const bool probeEmittedImmediately = !handler.longFrames.empty()
            && handler.longFrames.back() == probe;
        if (probeEmittedImmediately) {
            std::printf("Case A corroboration: a fresh clean DF17 for the "
                "same address was emitted on first sighting -- the address "
                "was already (wrongly) trusted\n");
            ++failures;
        }
    }

    // --- Case C: known but not yet trusted (one prior clean sighting) --
    // 0x1E7C42: a distinct address from case A/B, needed only so this case's
    // own DemodCore instance starts from a single untrusted sighting rather
    // than none or two.
    constexpr uint32_t knownUntrustedAddress = 0x1E7C42;
    {
        CollectHandler handler;
        DemodCore<1, CollectHandler> demod(handler);
        feedQuiet(demod, 1000);

        const auto first = makeDf17Position(knownUntrustedAddress, 40000, 40000, false);
        feedFrame(demod, first); // first sighting: inserted, untrusted
        feedQuiet(demod, 300);

        const auto repaired1 = makeDf17Position(knownUntrustedAddress, 70000, 71000, true);
        feedFrame(demod, asDf19WithBit108Error(repaired1));
        feedQuiet(demod, 300);

        if (!handler.longFrames.empty()) {
            std::printf("Case C (known, untrusted address): %zu frame(s) "
                "emitted from a DF19-shaped repair for an address with only "
                "one prior clean sighting -- a repair promoted a "
                "known-but-untrusted entry\n", handler.longFrames.size());
            ++failures;
        }

        // Corroborate: if that repair had wrongly promoted the entry to
        // trusted, a second DF19-shaped repair would now be allowed to
        // emit (recovery is legitimate for a trusted address, see case B).
        const auto repaired2 = makeDf17Position(knownUntrustedAddress, 72000, 73000, false);
        feedFrame(demod, asDf19WithBit108Error(repaired2));
        feedQuiet(demod, 300);

        if (!handler.longFrames.empty()) {
            std::printf("Case C corroboration: a second DF19-shaped repair "
                "emitted %zu frame(s) -- the first repair had already "
                "(wrongly) promoted the address to trusted\n",
                handler.longFrames.size());
            ++failures;
        }
    }

    // --- Case B: address already trusted through clean sightings -----
    constexpr uint32_t trustedAddress = 0x39B4C1;
    {
        CollectHandler handler;
        DemodCore<1, CollectHandler> demod(handler);
        feedQuiet(demod, 1000);

        const auto t1 = makeDf17Position(trustedAddress, 93000, 51372, false);
        const auto t2 = makeDf17Position(trustedAddress, 74158, 50194, true);
        feedFrame(demod, t1);
        feedQuiet(demod, 300);
        feedFrame(demod, t2); // second clean sighting: trusted, emitted
        feedQuiet(demod, 300);

        if (handler.longFrames.size() != 1 || handler.longFrames[0] != t2) {
            std::printf("Case B setup failed: address did not become "
                "trusted through two clean DF17 sightings as expected\n");
            ++failures;
        } else {
            const auto original = makeDf17Position(trustedAddress, 60000, 40000, false);
            const auto damaged = asDf19WithBit108Error(original);
            feedFrame(demod, damaged);
            feedQuiet(demod, 300);

            const bool recovered = handler.longFrames.size() == 2
                && handler.longFrames[1] == original;
            if (!recovered) {
                std::printf("Case B: recovery for an already-trusted address "
                    "did NOT emit the original DF17 frame (%zu long frame(s) "
                    "total)\n", handler.longFrames.size());
                ++failures;
            }
        }
    }

    if (failures == 0) {
        std::printf("DF19->17 promotion never seeds trust for a new address\n");
        return 0;
    }
    std::printf("FAILED with %d problem(s)\n", failures);
    return 1;
}
