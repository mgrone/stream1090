/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "ICAOCache.hpp"

namespace {

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
        && capabilityChangePreservesTrustedAircraft());
}
