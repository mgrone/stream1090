/* SPDX-License-Identifier: GPL-3.0-or-later */

// REGRESSION (fixed in production code).
//
// Upstream (edf006a) treats every DF19 candidate as a possibly-corrupted
// DF17: it flips bit 108 -- the DF field's second-least-significant bit,
// exactly where 17 (10001) and 19 (10011) differ -- and XORs the CRC by
// CRC::delta<108>() before the crc==0 check ever runs. When the flip lands
// on a real single-bit error, the reconstructed frame's CRC becomes zero,
// and handleExtSquitterLongMessage() used to treat it exactly like a
// genuinely clean squitter: on the "not known" branch this was the same
// door a real clean DF17 uses to earn trust for a brand-new address
// (confirmTrustCandidate + insertWithCA), with nothing distinguishing "this
// bit pattern really arrived over the air" from "this bit pattern is our
// own guess at a correction". Repeating a repair-shaped signal three times
// could therefore create trust for an address nobody had ever cleanly
// received, contrary to the principle stated elsewhere in this file:
// repairs must never seed trust for a new address, only extend it for one
// that already has it.
//
// Fixed by recording, before the DF19->17 promotion overwrites
// downlinkFormat, whether the frame arrived as DF19. A frame that only
// reaches crc==0 by way of the promotion is a repair, not a genuine clean
// reception: for any address not already trusted it now returns early,
// before confirmTrustCandidate() is ever called, so it neither seeds trust
// nor leaves a trust-candidate sighting behind for a later clean reception
// to complete. Recovery for an address already trusted through clean
// sightings is unaffected -- it is handled by the existing
// isTrusted()-gated branch, checked before the new guard.
//
// This test drives the fix through the public bit-level entry point only.
// Two independent cases:
//   A) a brand-new address, signal shaped so DF field flips 19->17 --
//      nothing may ever be emitted, and trust must never appear.
//   B) an address already trusted through clean DF17 sightings, then the
//      same kind of bit-108 error on a message for that same address --
//      the recovery is allowed to emit the corrected, original DF17 frame;
//      that is legitimate error correction for an address already vetted,
//      not trust creation.
//
// Passes on this branch since the fix: case A emits nothing and creates no
// trust; case B still recovers and emits the original frame. On edf006a
// (upstream, unfixed) case A is expected to fail (something gets emitted /
// trust appears for the new address); case B passes there (the promotion's
// whole point is to let this recovery through). Case A is expected to pass
// on 16075b9 (initial reference; the DF19->17 promotion does not exist
// there, and the general "repairs never seed trust for unknown addresses"
// rule already holds). Case B is asserted here exactly the same way on
// every reference -- a fix that discarded every DF19 candidate
// unconditionally, recovery included, must fail it too -- but on 16075b9 it
// is expected to fail for a different, unrelated reason: DF19 candidates
// reach the CRC error table there through the unmodified generic repair
// path, and that table is not known to cover this exact single-bit
// syndrome. Report case A and case B separately when comparing against
// 16075b9; a case B failure there is not evidence of anything on its own.
//
// This test does not call into Plausibility:: at all, so it compiles
// unmodified on every reference, 16075b9 included. In its place, a
// dedicated positive control (below) confirms with DemodCore's own public
// behavior -- not by inspecting a production API directly -- that the
// address used in case A is not rejected for some unrelated reason on
// whichever reference is under test.

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
// (nonzero) -- the exact signal shape upstream's DF19->17 promotion is
// built to "recover" back to this same DF17 frame.
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
    // 0x1A2B3C: known, on every reference this test targets, to pass
    // whatever address-plausibility screening exists there (verified below
    // by a live positive control, not by calling into Plausibility::
    // directly -- that would not compile on 16075b9). The point of case A
    // is that repairing a signal into a structurally-plausible-looking
    // crc==0 frame must still not create trust for a never-cleanly-received
    // address, a distinct invariant from "does the address/frame shape
    // look plausible": content checks are not meant to, and cannot, tell a
    // genuinely clean reception apart from a guessed repair. Using an
    // address that some unrelated screening would itself reject would
    // confound the two and pass this test for the wrong reason -- which is
    // exactly what the positive control below rules out.
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
                "(%zu long frame(s)) -- this address is rejected for some "
                "unrelated reason on this reference; case A below would not "
                "isolate the DF19 promotion bug, pick a different address\n",
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

    // fixture sanity: each signal really presents as DF 19 with the
    // syndrome the bit-108 flip predicts, and reconstructs a genuinely
    // clean DF17 frame -- verified independently of DemodCore, with the
    // same CRC::delta<> upstream's own promotion uses.
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

        // Corroborate with an independent path: if trust had been created,
        // a later genuinely clean DF17 for the same address would land on
        // the "already trusted, renew immediately" branch and be emitted
        // on its very first (post-promotion) sighting, rather than needing
        // its own separate two-sighting confirmation.
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
            if (recovered) {
                std::printf("Case B: recovery for an already-trusted address "
                    "emitted the original DF17 frame, as intended\n");
            } else {
                std::printf("Case B: recovery for an already-trusted address "
                    "did NOT emit the original DF17 frame (%zu long frame(s) "
                    "total) -- a fix that discarded every DF19 candidate "
                    "unconditionally, recovery included, would fail here too; "
                    "report separately from case A, do not conflate the two\n",
                    handler.longFrames.size());
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
