/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "ICAOCache.hpp"

#include <cmath>

namespace {

// Squawk/altitude state is touched by address-parity traffic on the hot path.
// Keep CPR history in separate cold storage instead of inflating every entry.
static_assert(sizeof(ICAOTable::SquawkAlt) <= 8);

void tick(ICAOTable& table, uint32_t count) {
    while (count-- > 0)
        table.tick();
}

bool confirmsOnlySeparateSightings() {
    ICAOTable table;
    constexpr uint32_t icaoWithCA = 0x5abcde1;

    if (table.confirmDF11Candidate(icaoWithCA))
        return false;
    if (table.confirmDF11Candidate(icaoWithCA))
        return false;

    tick(table, ICAOTable::DF11CandidateMinTicks);
    if (!table.confirmDF11Candidate(icaoWithCA))
        return false;
    return !table.confirmDF11Candidate(icaoWithCA);
}

bool expiresOldSightings() {
    ICAOTable table;
    constexpr uint32_t icaoWithCA = 0x3abcdef;

    if (table.confirmDF11Candidate(icaoWithCA))
        return false;
    tick(table, ICAOTable::DF11CandidateMaxTicks + 1);
    return !table.confirmDF11Candidate(icaoWithCA);
}

bool hashCollisionsCannotConfirmAnotherAddress() {
    ICAOTable table;
    constexpr uint32_t first = 0x0100001;
    uint32_t collision = first + 1;

    while (((first * 0x9e3779b1u) >> 24) != ((collision * 0x9e3779b1u) >> 24))
        collision++;

    if (table.confirmDF11Candidate(first))
        return false;
    tick(table, ICAOTable::DF11CandidateMinTicks);
    if (table.confirmDF11Candidate(collision))
        return false;
    tick(table, ICAOTable::DF11CandidateMinTicks);
    return !table.confirmDF11Candidate(first);
}

bool rejectedFramesNeedIndependentConfirmation() {
    ICAOTable table;
    constexpr uint32_t icao = 0xabcdef;
    constexpr uint64_t frame = 0x0200102acac271;

    if (table.confirmRejectedShort(icao, frame))
        return false;
    if (table.confirmRejectedShort(icao, frame))
        return false;

    tick(table, ICAOTable::RejectedCandidateMinTicks);
    if (!table.confirmRejectedShort(icao, frame))
        return false;
    return !table.confirmRejectedShort(icao, frame ^ 1);
}

bool expiredRejectedFramesNeedNewPair() {
    ICAOTable table;
    constexpr uint32_t icao = 0xabcdef;
    constexpr uint64_t frame = 0x0200102acac271;

    if (table.confirmRejectedShort(icao, frame))
        return false;
    tick(table, ICAOTable::RejectedCandidateMaxTicks + 1);
    return !table.confirmRejectedShort(icao, frame);
}

bool shortAndLongCandidatesCannotConfirmEachOther() {
    ICAOTable table;
    constexpr uint32_t icao = 0xabcdef;
    constexpr uint64_t frame = 0x0200102acac271;

    if (table.confirmRejectedShort(icao, frame))
        return false;
    tick(table, ICAOTable::RejectedCandidateMinTicks);
    if (table.confirmRejectedLong(icao, 0, frame))
        return false;
    tick(table, ICAOTable::RejectedCandidateMinTicks);
    return table.confirmRejectedLong(icao, 0, frame);
}

bool emptySlotsRejectUnknownAddresses() {
    ICAOTable table;
    constexpr uint32_t unknownWithCA = 0x5abcde1;

    return !table.findWithCA(unknownWithCA).isValid()
        && !table.find(unknownWithCA & 0xffffffu).isValid()
        && table.findWithCA(0).isValid()
        && table.find(0).isValid();
}

bool insertedAndReplacementEntriesAreFound() {
    ICAOTable table;
    constexpr uint32_t first = 0x1abcde;
    constexpr uint32_t replacement = 0x2abcde;

    table.insertWithCA(first);
    if (!table.findWithCA(first).isValid() || !table.find(first).isValid())
        return false;

    table.insertWithCA(replacement);
    return !table.findWithCA(first).isValid()
        && !table.find(first).isValid()
        && table.findWithCA(replacement).isValid()
        && table.find(replacement).isValid();
}

bool expiredEntriesDisappear() {
    ICAOTable table;
    constexpr uint32_t icaoWithCA = 0x5000001;

    const auto entry = table.insertWithCA(icaoWithCA);
    table.markAsSeen(entry, 1);
    tick(table, 1);
    if (!table.findWithCA(icaoWithCA).isValid())
        return false;

    tick(table, 1'000'000);
    return !table.findWithCA(icaoWithCA).isValid()
        && !table.find(icaoWithCA & 0xffffffu).isValid();
}

bool validationOnlyAltitudeCannotPoisonState() {
    ICAOTable table;
    const auto entry = table.insertWithCA(0x5abcde1);

    if (table.checkAltitude(entry, 0, false))
        return false;
    if (table.checkAltitude(entry, 10000, false))
        return false;
    if (table.checkAltitude(entry, 10000))
        return false;
    if (!table.checkAltitude(entry, 10000))
        return false;
    if (!table.checkAltitude(entry, 10500, false))
        return false;
    if (table.checkAltitude(entry, 13000, false))
        return false;
    return table.checkAltitude(entry, 10000);
}

bool firstLowAltitudeNeedsConfirmation() {
    ICAOTable table;
    const auto entry = table.insertWithCA(0x5abcde1);

    if (table.checkAltitude(entry, -500))
        return false;
    return table.checkAltitude(entry, -500);
}

bool capabilityChangePreservesTrustedAircraft() {
    ICAOTable table;
    constexpr uint32_t ca5 = 0x5abcde1;
    constexpr uint32_t ca7 = 0x7abcde1;

    const auto original = table.insertWithCA(ca5);
    table.markAsTrustedSeen(original);
    const auto refreshed = table.insertWithCA(ca7);

    return refreshed.key == original.key
        && !table.findWithCA(ca5).isValid()
        && table.findWithCA(ca7).isValid()
        && table.find(ca5 & 0xffffffu).isValid()
        && table.isTrusted(refreshed);
}

bool cleanCprPairSeedsPosition() {
    ICAOTable table;
    const auto entry = table.insertWithCA(0x5abcde1);
    constexpr uint64_t now = 1'000;

    table.noteCprClean(entry, false, 93000, 51372, now, 10'000);
    int32_t lat = 0;
    int32_t lon = 0;
    if (table.cachedPosition(entry, lat, lon, now, 60'000))
        return false;

    // the odd frame arrived last, so the fix is the odd solution
    // 52.265780 N, 3.938913 E - not the even solution 52.257202, 3.919373
    table.noteCprClean(entry, true, 74158, 50194, now + 100, 10'000);
    return table.cachedPosition(entry, lat, lon, now + 100, 60'000)
        && std::abs(lat - 5'226'578) < 10
        && std::abs(lon - 393'891) < 10
        && table.cachedPosition(entry, lat, lon, now + 60'100, 60'000)
        && !table.cachedPosition(entry, lat, lon, now + 60'101, 60'000);
}

bool evenAfterOddRecordsEvenSolution() {
    ICAOTable table;
    const auto entry = table.insertWithCA(0x5abcde2);
    constexpr uint64_t now = 2'000;

    table.noteCprClean(entry, true, 74158, 50194, now, 10'000);
    int32_t lat = 0;
    int32_t lon = 0;
    if (table.cachedPosition(entry, lat, lon, now, 60'000))
        return false;

    // now the even frame is the more recent one
    table.noteCprClean(entry, false, 93000, 51372, now + 100, 10'000);
    return table.cachedPosition(entry, lat, lon, now + 100, 60'000)
        && std::abs(lat - 5'225'720) < 10
        && std::abs(lon - 391'937) < 10;
}

bool southernPairSeedsNegativeReference() {
    ICAOTable table;
    const auto entry = table.insertWithCA(0x5abcde3);
    constexpr uint64_t now = 3'000;

    // a pair decoding to 33.55000 S, 70.79999 W: both reference components
    // are negative, which is where the sign-safe decode has to hold up
    table.noteCprClean(entry, false, 53521, 47623, now, 10'000);
    table.noteCprClean(entry, true, 65736, 73400, now + 100, 10'000);
    int32_t lat = 0;
    int32_t lon = 0;
    return table.cachedPosition(entry, lat, lon, now + 100, 60'000)
        && std::abs(lat + 3'355'000) < 10
        && std::abs(lon + 7'080'000) < 10;
}

bool cachedOppositeCprPairsAndAges() {
    ICAOTable table;
    const auto entry = table.insertWithCA(0x5abcde4);
    constexpr uint64_t now = 4'000;

    // Clean observations seed the trusted reference, but a frame withheld by
    // the demodulator must not pretend to be available to downstream decoders.
    table.noteCprClean(entry, false, 93000, 51372, now, 10'000);
    uint32_t latCpr = 0;
    uint32_t lonCpr = 0;
    if (table.cachedOppositeCpr(entry, true, latCpr, lonCpr, now, 10'000))
        return false;

    table.noteCprOutput(entry, false, 93000, 51372, now);

    // an odd frame pairs against the stored even bits
    if (!table.cachedOppositeCpr(entry, true, latCpr, lonCpr, now, 10'000))
        return false;
    if (latCpr != 93000 || lonCpr != 51372)
        return false;

    // nothing is stored for the even parity itself yet
    if (table.cachedOppositeCpr(entry, false, latCpr, lonCpr, now, 10'000))
        return false;

    table.noteCprOutput(entry, true, 74158, 50194, now + 100);
    // once the odd parity exists, an even frame pairs against it ...
    if (!table.cachedOppositeCpr(entry, false, latCpr, lonCpr, now + 100, 10'000))
        return false;
    if (latCpr != 74158 || lonCpr != 50194)
        return false;

    // ... through the exact pair-window boundary, but not one tick beyond it
    return table.cachedOppositeCpr(entry, false, latCpr, lonCpr,
            now + 100 + 10'000, 10'000)
        && !table.cachedOppositeCpr(entry, false, latCpr, lonCpr,
            now + 100 + 10'001, 10'000);
}

} // namespace

int main() {
    return !(confirmsOnlySeparateSightings()
        && expiresOldSightings()
        && hashCollisionsCannotConfirmAnotherAddress()
        && rejectedFramesNeedIndependentConfirmation()
        && expiredRejectedFramesNeedNewPair()
        && shortAndLongCandidatesCannotConfirmEachOther()
        && emptySlotsRejectUnknownAddresses()
        && insertedAndReplacementEntriesAreFound()
        && expiredEntriesDisappear()
        && validationOnlyAltitudeCannotPoisonState()
        && firstLowAltitudeNeedsConfirmation()
        && capabilityChangePreservesTrustedAircraft()
        && cleanCprPairSeedsPosition()
        && evenAfterOddRecordsEvenSolution()
        && southernPairSeedsNegativeReference()
        && cachedOppositeCprPairsAndAges());
}
