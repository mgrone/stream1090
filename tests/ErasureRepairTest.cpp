/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "ErasureRepair.hpp"
#include "ICAOCache.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

namespace {

std::mt19937_64 rng(20260729);

// A frame whose CRC-24 is zero, i.e. a well formed extended squitter.
Bits128 makeValidFrame() {
    Bits128 frame(rng(), rng());
    frame.low() &= ~uint64_t(0xFFFFFF);
    frame.low() |= (CRC::compute<112>(frame) & 0xFFFFFF);
    return frame;
}

void flipBit(Bits128& frame, uint8_t index) {
    Bits128 one(uint64_t(1));
    one.shiftLeft(index);
    frame = frame ^ one;
}

// The runtime table must agree with the compile time deltas it mirrors.
bool deltaTableMatchesCRC() {
    const auto& delta = ErasureRepair::deltaTable();
    return delta[0] == CRC::delta<0>()
        && delta[1] == CRC::delta<1>()
        && delta[55] == CRC::delta<55>()
        && delta[111] == CRC::delta<111>();
}

// Errors placed inside the candidate set must be recovered exactly.
bool recoversErrorsInsideCandidateSet() {
    for (int errors = 1; errors <= 12; ++errors) {
        for (int trial = 0; trial < 200; ++trial) {
            Bits128 frame = makeValidFrame();

            std::vector<uint8_t> candidates(ErasureRepair::MaxFrameBits);
            std::iota(candidates.begin(), candidates.end(), uint8_t(0));
            std::shuffle(candidates.begin(), candidates.end(), rng);
            candidates.resize(ErasureRepair::Candidates);
            if (errors > int(candidates.size()))
                continue;

            uint32_t expected = 0;
            for (int e = 0; e < errors; ++e) {
                flipBit(frame, candidates[e]);
                expected |= (uint32_t(1) << e);
            }

            const auto result = ErasureRepair::solve(CRC::compute<112>(frame),
                                                     candidates.data(), candidates.size());
            if (!result.solved || result.mask != expected)
                return false;

            // applying the recovered pattern must clear the CRC
            for (size_t i = 0; i < candidates.size(); ++i)
                if ((result.mask >> i) & 1)
                    flipBit(frame, candidates[i]);
            if (CRC::compute<112>(frame) != 0)
                return false;
        }
    }
    return true;
}

// An undamaged frame has a zero syndrome and needs no flips.
bool leavesCleanFramesAlone() {
    std::vector<uint8_t> candidates(ErasureRepair::Candidates);
    std::iota(candidates.begin(), candidates.end(), uint8_t(0));
    for (int trial = 0; trial < 200; ++trial) {
        const auto frame = makeValidFrame();
        const auto result = ErasureRepair::solve(CRC::compute<112>(frame),
                                                 candidates.data(), candidates.size());
        if (!result.solved || result.mask != 0)
            return false;
    }
    return true;
}

// The spurious-solve rate is what bounds false repairs, so pin it: offering k
// candidates must accept a random syndrome with probability near 2^(k-24).
bool spuriousSolveRateMatchesTheory() {
    constexpr int k = 12;
    constexpr int trials = 200000;

    std::vector<uint8_t> candidates(ErasureRepair::MaxFrameBits);
    std::iota(candidates.begin(), candidates.end(), uint8_t(0));
    std::shuffle(candidates.begin(), candidates.end(), rng);
    candidates.resize(k);

    int solved = 0;
    for (int i = 0; i < trials; ++i) {
        const CRC::crc_t syndrome = CRC::crc_t(rng() & 0xFFFFFF);
        if (ErasureRepair::solve(syndrome, candidates.data(), candidates.size()).solved)
            solved++;
    }

    const double measured = double(solved) / trials;
    const double expected = 1.0 / double(1u << (24 - k));
    // generous band; this is a guard against the rate changing by orders of
    // magnitude, not a distributional test
    return measured > expected * 0.5 && measured < expected * 2.0;
}

// The trusted-membership filter must never be clear for a trusted entry,
// otherwise a repair that should be accepted is silently dropped.
bool trustedFilterNeverMissesATrustedEntry() {
    ICAOTable table;

    for (uint32_t i = 0; i < 500; ++i) {
        const uint32_t icaoWithCA = 0x5000000u | (i * 7919u);
        const auto entry = table.insertWithCA(icaoWithCA);
        table.markAsTrustedSeen(entry);

        if (!table.maybeTrusted(icaoWithCA))
            return false;
        if (!table.isTrusted(table.findWithCA(icaoWithCA)))
            return false;
    }
    return true;
}

// And it must actually reject: a table with nothing trusted rejects everything.
bool trustedFilterRejectsUnknownAddresses() {
    ICAOTable table;
    int accepted = 0;
    for (uint32_t i = 0; i < 20000; ++i)
        if (table.maybeTrusted(0x4000000u | (i * 104729u)))
            accepted++;
    return accepted == 0;
}

// The shipped configuration accepts a repair only if the solution is also
// light enough. That combined rate -- not the raw solve rate -- is what bounds
// false repairs in production, so pin it against the closed form.
bool weightCappedAcceptanceMatchesTheory() {
    constexpr int k = ErasureRepair::Candidates;
    constexpr int w = ErasureRepair::MaxWeight;
    constexpr int trials = 400000;

    std::vector<uint8_t> candidates(ErasureRepair::MaxFrameBits);
    std::iota(candidates.begin(), candidates.end(), uint8_t(0));
    std::shuffle(candidates.begin(), candidates.end(), rng);
    candidates.resize(k);

    int accepted = 0;
    for (int i = 0; i < trials; ++i) {
        const CRC::crc_t syndrome = CRC::crc_t(rng() & 0xFFFFFF);
        const auto r = ErasureRepair::solve(syndrome, candidates.data(), candidates.size());
        if (r.solved && __builtin_popcount(r.mask) <= w)
            accepted++;
    }

    // sum_{i<=w} C(k,i) / 2^24
    double ways = 0.0;
    for (int i = 0; i <= w; ++i) {
        double c = 1.0;
        for (int j = 0; j < i; ++j)
            c = c * (k - j) / (j + 1);
        ways += c;
    }
    const double expected = ways / double(1u << 24);
    const double measured = double(accepted) / trials;
    return measured > expected * 0.5 && measured < expected * 2.0;
}

} // namespace

int main() {
    if (!deltaTableMatchesCRC())
        return 1;
    if (!recoversErrorsInsideCandidateSet())
        return 1;
    if (!leavesCleanFramesAlone())
        return 1;
    if (!spuriousSolveRateMatchesTheory())
        return 1;
    if (!weightCappedAcceptanceMatchesTheory())
        return 1;
    if (!trustedFilterNeverMissesATrustedEntry())
        return 1;
    if (!trustedFilterRejectsUnknownAddresses())
        return 1;
    return 0;
}
