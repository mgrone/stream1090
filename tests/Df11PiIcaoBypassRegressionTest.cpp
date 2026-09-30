/* SPDX-License-Identifier: GPL-3.0-or-later */

// REGRESSION (fixed in production code; was specific to this branch --
// upstream never had this bug).
//
// On this branch, DF11's crc<80 (PI overlaid with an interrogator code)
// path, once confirmDF11Candidate() confirms a second sighting, used to
// insert the address and mark it trusted directly, with no address check:
//
//     const auto it = e.isValid() ? e : m_cache.insertWithCA(icaoWithCA);
//     m_cache.markAsTrustedSeen(it);
//
// Every other insertion point added by upstream's plausibility check
// (DF17's and DF11's own crc==0 "not known" branches, and the shared
// handleDF11ShortMessageWithZeroCRC() helper the crc==0 and crc<80 paths
// used to both funnel through before this branch's trust-gate rework)
// calls Plausibility::checkICAO() before insertWithCA(). This one direct
// call was introduced by this branch's own restructuring of the crc<80
// path (it used to reach the shared helper, which upstream's version still
// does) and did not. An address checkICAO() would reject could gain trust
// through repeated PI-overlaid DF11 replies alone.
//
// Fixed by adding the same checkICAO() guard, scoped to a fresh insert
// only (an already-known-but-untrusted entry was already screened at its
// own insertion, and promoting it here is unaffected), matching the other
// three insertion points.
//
// This test only checks that this branch's insertion points are as
// consistent with each other as upstream's are; it makes no claim that the
// checkICAO() whitelist itself is complete or standard-correct.
//
// Driven through the public bit-level entry point only.
//
// Passes on edf006a (upstream, unaffected) and on this branch since the
// fix. The initial reference (16075b9) predates checkICAO() entirely and
// is not expected to enforce this policy either way.

#include "DemodCore.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct CollectHandler {
    void handleShort(uint64_t, uint64_t frame) { shortFrames.push_back(frame); }
    void handleLong(uint64_t, const Bits128&) {}

    std::vector<uint64_t> shortFrames;
};

uint64_t df11Data(uint32_t icao, uint8_t capability) {
    return (0b01011ull << 51) | (uint64_t(capability) << 48) | (uint64_t(icao) << 24);
}

// DF11 all-call reply with an interrogator (PI) overlay: the transmitted
// parity is the plain CRC XORed with a small code, so recomputing the CRC
// over the whole received frame yields exactly that code back (the
// syndrome), the same construction DF11InterrogatorTest.cpp and
// TrustGateTest.cpp both use.
uint64_t makeDf11Interrogated(uint32_t icao, uint8_t capability, uint32_t code) {
    const uint64_t data = df11Data(icao, capability);
    return data | (CRC::compute<56>(Bits128(data)) ^ code);
}

uint64_t makeDf5(uint32_t icao, uint16_t squawk) {
    const uint64_t data = (0b00101ull << 51) | (uint64_t(squawk) << 24);
    return data | (CRC::compute<56>(Bits128(data)) ^ icao);
}

void feedFrame(DemodCore<1, CollectHandler>& demod, uint64_t frame) {
    for (int bit = 55; bit >= 0; --bit) {
        uint32_t value[] = { uint32_t((frame >> bit) & 1) };
        demod.shiftInNewBits(value);
    }
}

void feedQuiet(DemodCore<1, CollectHandler>& demod, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        uint32_t value[] = { 0 };
        demod.shiftInNewBits(value);
    }
}

} // namespace

int main() {
    int failures = 0;

    // --- Precondition: the address used below is really rejected -----
    constexpr uint32_t implausibleAddress = 0xF12345;
    if (Plausibility::checkICAO(implausibleAddress)) {
        std::printf("fixture error: 0xF12345 is not rejected by "
            "Plausibility::checkICAO() -- pick a different address\n");
        return 1;
    }

    // --- Positive control: an address checkICAO() admits -------------
    // Exercises the existing, intended contract end to end (unaffected by
    // this bug): the confirming second PI-overlaid sighting stays silent,
    // and the next sighting -- now that the address is trusted -- emits.
    // If this fails, the harness itself is broken, not the bypass below.
    constexpr uint32_t admissibleAddress = 0xABCDEF;
    if (!Plausibility::checkICAO(admissibleAddress)) {
        std::printf("fixture error: 0xABCDEF is unexpectedly rejected by "
            "Plausibility::checkICAO()\n");
        return 1;
    }
    {
        CollectHandler handler;
        DemodCore<1, CollectHandler> demod(handler);
        feedQuiet(demod, 1000);

        const auto first = makeDf11Interrogated(admissibleAddress, 5, 1);
        const auto second = makeDf11Interrogated(admissibleAddress, 5, 2);
        const auto third = makeDf11Interrogated(admissibleAddress, 5, 3);
        // the same code the "third" reply would carry once its overlay is
        // stripped -- the exact value the confirmed sighting must emit
        const auto thirdOverlayStripped = makeDf11Interrogated(admissibleAddress, 5, 0);
        if (CRC::compute<56>(Bits128(first)) != 1
                || CRC::compute<56>(Bits128(second)) != 2
                || CRC::compute<56>(Bits128(third)) != 3) {
            std::printf("fixture error: PI-overlaid syndromes do not match "
                "the intended codes\n");
            return 1;
        }

        feedFrame(demod, first);
        feedQuiet(demod, 300); // > 100 us, < 2 s; let dispatch complete
        if (handler.shortFrames.size() != 0) {
            std::printf("positive control failed: first PI-overlaid sighting "
                "emitted %zu frame(s), expected none -- one sighting proves "
                "nothing\n", handler.shortFrames.size());
            ++failures;
        }

        feedFrame(demod, second); // confirms: trusted, stays silent
        feedQuiet(demod, 300);
        if (handler.shortFrames.size() != 0) {
            std::printf("positive control failed: confirming (second) "
                "PI-overlaid sighting emitted %zu frame(s), expected none -- "
                "this branch keeps the confirming sighting silent\n",
                handler.shortFrames.size());
            ++failures;
        }

        feedFrame(demod, third); // now trusted: emits, overlay stripped
        feedQuiet(demod, 300); // let dispatch complete before checking
        if (handler.shortFrames.size() != 1
                || handler.shortFrames[0] != thirdOverlayStripped) {
            std::printf("positive control failed: third PI-overlaid sighting "
                "(now trusted) should emit exactly the DF11 with its overlay "
                "stripped; got %zu frame(s)\n", handler.shortFrames.size());
            ++failures;
        }
    }

    // --- The actual regression ----------------------------------------
    {
        CollectHandler handler;
        DemodCore<1, CollectHandler> demod(handler);
        feedQuiet(demod, 1000);

        const auto first = makeDf11Interrogated(implausibleAddress, 5, 1);
        const auto second = makeDf11Interrogated(implausibleAddress, 5, 2);
        const auto third = makeDf11Interrogated(implausibleAddress, 5, 3);
        if (CRC::compute<56>(Bits128(first)) != 1
                || CRC::compute<56>(Bits128(second)) != 2
                || CRC::compute<56>(Bits128(third)) != 3) {
            std::printf("fixture error: PI-overlaid syndromes do not match "
                "the intended codes\n");
            return 1;
        }

        feedFrame(demod, first);
        feedQuiet(demod, 300); // > 100 us, < 2 s
        feedFrame(demod, second); // if confirmed, this branch trusts it here
        feedQuiet(demod, 300);
        feedFrame(demod, third);
        feedQuiet(demod, 1000);

        if (!handler.shortFrames.empty()) {
            std::printf("%zu frame(s) emitted for an address "
                "Plausibility::checkICAO() rejects: the crc<80 confirming "
                "path inserted and trusted 0x%X without ever checking it\n",
                handler.shortFrames.size(), implausibleAddress);
            ++failures;
        }

        // Corroborate independently: repeated DF5 with a non-zero squawk
        // recovers the address purely from address parity and requires
        // trust. Only frames emitted from here on count: record where the
        // vector stood before this sequence and check just the new tail, so
        // an earlier DF11 emission above (if any) is never attributed to
        // DF5 -- reusing the whole vector unconditionally would do exactly
        // that.
        const std::size_t beforeDf5 = handler.shortFrames.size();
        feedFrame(demod, makeDf5(implausibleAddress, 0x1234));
        feedQuiet(demod, 300);
        feedFrame(demod, makeDf5(implausibleAddress, 0x1234)); // matching squawk confirms
        feedQuiet(demod, 300);

        if (handler.shortFrames.size() != beforeDf5) {
            std::printf("%zu new frame(s) emitted on DF5 for an address "
                "Plausibility::checkICAO() rejects (short=%014llX): it was "
                "promoted indirectly by the DF11 PI-overlay sequence above\n",
                handler.shortFrames.size() - beforeDf5,
                (unsigned long long)handler.shortFrames.back());
            ++failures;
        }
    }

    // --- Positive control: the same DF5 corroboration can emit ---------
    // Establishes trust for an admissible address the same way as the
    // first positive control, then runs the identical two-DF5 sequence:
    // proves this DF5 construction and the checkSquawk() corroboration it
    // relies on actually work end to end, so a silent DF5 above is
    // meaningful and not just an unrelated dead end.
    {
        CollectHandler handler;
        DemodCore<1, CollectHandler> demod(handler);
        feedQuiet(demod, 1000);

        feedFrame(demod, makeDf11Interrogated(admissibleAddress, 5, 1));
        feedQuiet(demod, 300);
        feedFrame(demod, makeDf11Interrogated(admissibleAddress, 5, 2)); // confirms
        feedQuiet(demod, 300);
        feedFrame(demod, makeDf11Interrogated(admissibleAddress, 5, 3)); // emits
        feedQuiet(demod, 300);

        const std::size_t beforeDf5 = handler.shortFrames.size();
        feedFrame(demod, makeDf5(admissibleAddress, 0x1234));
        feedQuiet(demod, 300);
        feedFrame(demod, makeDf5(admissibleAddress, 0x1234)); // matching squawk confirms
        feedQuiet(demod, 300);

        if (handler.shortFrames.size() != beforeDf5 + 1) {
            std::printf("positive control failed: DF5 with a matching squawk "
                "did not emit exactly one new frame for the trusted, "
                "admissible address (got %zu new)\n",
                handler.shortFrames.size() - beforeDf5);
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("DF11 PI overlay respects Plausibility::checkICAO()\n");
        return 0;
    }
    std::printf("FAILED with %d problem(s)\n", failures);
    return 1;
}
