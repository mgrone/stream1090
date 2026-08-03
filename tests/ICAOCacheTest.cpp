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

} // namespace

int main() {
    return !(confirmsOnlySeparateSightings()
        && expiresOldSightings()
        && hashCollisionsCannotConfirmAnotherAddress()
        && emptySlotsRejectUnknownAddresses()
        && insertedAndReplacementEntriesAreFound()
        && expiredEntriesDisappear());
}
