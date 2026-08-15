/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once
#include <numeric>
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <bit>
#include <fstream>
#include <sstream>
#include <vector>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include "Sampler.hpp"
#include "CustomFilterTaps.hpp"

/*
 * How the filter is evaluated
 * ---------------------------
 * The output is
 *
 *     y[n] = sum_{k=0..N-1} taps[k] * x[n - (B-1) + k]      B = bit_ceil(N)
 *
 * so the taps run over B consecutive input samples ending at x[n]. Written per
 * sample against a ring buffer, every tap needs its own masked index, the whole
 * thing stays scalar, and the tap sum ends in a horizontal reduction.
 *
 * The block form flips the two loops. The samples of a block are copied behind
 * the B-1 samples of history, which makes the input contiguous, and then the
 * outer loop runs over the taps while the inner one produces four output
 * samples at a time:
 *
 *     acc[0..3] += taps[k] * w[i+k .. i+k+3]
 *
 * Four independent accumulators, one tap shared across them, and no horizontal
 * reduction at all. That inner group of four is what the vectorizer turns into
 * a single vector FMA, so the shape is left to the compiler rather than
 * written out in intrinsics.
 *
 * Summation stays in tap order, so each output accumulates its taps in the
 * same sequence the plain scalar loop would use.
 */
namespace FirDetail {

    // tap count rounded up to a whole group of four
    constexpr size_t padTapCount(size_t n) noexcept {
        return (n + 3u) & ~size_t(3u);
    }

    // taps live in Q15, samples in Q14, so a product is Q29 and a whole
    // tap sum still fits an int32 as long as the taps sum to about one
    inline constexpr int TapFracBits = 15;

    constexpr int16_t toQ15(float t) noexcept {
        const float x = t * float(1 << TapFracBits);
        return int16_t(x >= 0.0f ? x + 0.5f : x - 0.5f);
    }

    /// Filters one contiguous block. w* hold history followed by the new
    /// samples, so w*[i + k] is the k-th tap partner of output i. Symmetric
    /// integer taps may pair their samples before multiplying: unlike the old
    /// float path this preserves the final rounded result exactly.
    template<bool Symmetric>
    inline void firBlock(const int16_t* __restrict taps, size_t numTaps,
                         const int16_t* __restrict wI, const int16_t* __restrict wQ,
                         int16_t* __restrict outI, int16_t* __restrict outQ,
                         size_t n) noexcept {
        size_t i = 0;

        // The inner group of four is what the vectorizer turns into a widening
        // multiply accumulate. Four int32 accumulators to a vector, the same
        // count float had, but the operands are half the width.
        for (; i + 4 <= n; i += 4) {
#if defined(__ARM_NEON)
            int32x4_t accI = vdupq_n_s32(0);
            int32x4_t accQ = vdupq_n_s32(0);

            if constexpr (Symmetric) {
                const size_t half = numTaps / 2;
                for (size_t k = 0; k < half; ++k) {
                    const size_t opposite = numTaps - 1 - k;
                    const int32x4_t pairI = vaddl_s16(vld1_s16(wI + i + k),
                                                      vld1_s16(wI + i + opposite));
                    const int32x4_t pairQ = vaddl_s16(vld1_s16(wQ + i + k),
                                                      vld1_s16(wQ + i + opposite));
                    accI = vmlaq_n_s32(accI, pairI, taps[k]);
                    accQ = vmlaq_n_s32(accQ, pairQ, taps[k]);
                }
                if (numTaps & 1) {
                    accI = vmlal_n_s16(accI, vld1_s16(wI + i + half), taps[half]);
                    accQ = vmlal_n_s16(accQ, vld1_s16(wQ + i + half), taps[half]);
                }
            } else {
                for (size_t k = 0; k < numTaps; ++k) {
                    accI = vmlal_n_s16(accI, vld1_s16(wI + i + k), taps[k]);
                    accQ = vmlal_n_s16(accQ, vld1_s16(wQ + i + k), taps[k]);
                }
            }

            const int32x4_t rounding = vdupq_n_s32(1 << (TapFracBits - 1));
            vst1_s16(outI + i, vshrn_n_s32(vaddq_s32(accI, rounding), TapFracBits));
            vst1_s16(outQ + i, vshrn_n_s32(vaddq_s32(accQ, rounding), TapFracBits));
#else
            int32_t accI[4] = { 0, 0, 0, 0 };
            int32_t accQ[4] = { 0, 0, 0, 0 };

            for (size_t k = 0; k < numTaps; ++k) {
                const int32_t t = taps[k];
                for (size_t l = 0; l < 4; ++l) {
                    accI[l] += t * int32_t(wI[i + k + l]);
                    accQ[l] += t * int32_t(wQ[i + k + l]);
                }
            }

            for (size_t l = 0; l < 4; l++) {
                outI[i + l] = int16_t((accI[l] + (1 << (TapFracBits - 1))) >> TapFracBits);
                outQ[i + l] = int16_t((accQ[l] + (1 << (TapFracBits - 1))) >> TapFracBits);
            }
#endif
        }

        // whatever does not fill a vector
        for (; i < n; i++) {
            int32_t sumI = 0;
            int32_t sumQ = 0;
            for (size_t k = 0; k < numTaps; ++k) {
                sumI += int32_t(taps[k]) * int32_t(wI[i + k]);
                sumQ += int32_t(taps[k]) * int32_t(wQ[i + k]);
            }
            outI[i] = int16_t((sumI + (1 << (TapFracBits - 1))) >> TapFracBits);
            outQ[i] = int16_t((sumQ + (1 << (TapFracBits - 1))) >> TapFracBits);
        }
    }

    // how many samples the block filter works on in one go
    inline constexpr size_t ChunkSize = 256;

} // namespace FirDetail


template<SampleRate inputRate, SampleRate outputRate>
class IQLowPass {
public:
    IQLowPass() {
        std::fill(std::begin(m_historyI), std::end(m_historyI), int16_t(0));
        std::fill(std::begin(m_historyQ), std::end(m_historyQ), int16_t(0));
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "[IQLowPass] tap count: " << numTaps << " symmetric: " << areTapsSymmetric << "\n";
        oss << "[IQLowPass] taps: (";
        for (size_t i = 0; i < numTaps; i++) {
            oss << taps[i]; //std::bit_cast<uint32_t>(taps[i]);
            if (i + 1 < numTaps) {
                oss << ", ";
            }
        }
        oss << ")";
        return oss.str();
    }

    /// Filters a whole block in place. This is the path the input reader takes.
    void applyBlock(int16_t* __restrict I, int16_t* __restrict Q, size_t n) noexcept {
        alignas(32) int16_t workI[WorkSize];
        alignas(32) int16_t workQ[WorkSize];

        for (size_t base = 0; base < n; base += FirDetail::ChunkSize) {
            const size_t m = std::min(FirDetail::ChunkSize, n - base);

            // history, then the fresh samples
            std::copy(m_historyI, m_historyI + HistorySize, workI);
            std::copy(m_historyQ, m_historyQ + HistorySize, workQ);
            std::copy(I + base, I + base + m, workI + HistorySize);
            std::copy(Q + base, Q + base + m, workQ + HistorySize);
            // the zero taps of the last vector may reach past the samples, so
            // make sure they land on zeros and not on stack garbage
            std::fill(workI + HistorySize + m, workI + HistorySize + m + numPaddedTaps, int16_t(0));
            std::fill(workQ + HistorySize + m, workQ + HistorySize + m + numPaddedTaps, int16_t(0));

            FirDetail::firBlock<areTapsSymmetric>(paddedTaps.data(), numTaps,
                                workI, workQ, I + base, Q + base, m);

            // the tail of the work buffer is the history of the next chunk
            std::copy(workI + m, workI + m + HistorySize, m_historyI);
            std::copy(workQ + m, workQ + m + HistorySize, m_historyQ);
        }
    }

    // single sample entry point, kept for pipelines that are driven per sample
    void apply(int16_t& value_I, int16_t& value_Q) noexcept {
        applyBlock(&value_I, &value_Q, 1);
    }

private:
    static constexpr auto taps = LowPassTaps::getCustomTaps<inputRate, outputRate>();
    static constexpr auto numTaps = taps.size();
    static constexpr auto bufferSize = std::bit_ceil(numTaps);
    static constexpr bool areTapsOdd = LowPassTaps::areCustomTapsOdd<inputRate, outputRate>();
    static constexpr bool areTapsSymmetric = LowPassTaps::areCustomTapsSymmetric<inputRate, outputRate>();

    static constexpr size_t numPaddedTaps = FirDetail::padTapCount(numTaps);

    // the window of an output reaches B-1 samples back
    static constexpr size_t HistorySize = bufferSize - 1;

    // taps converted to Q15 and zero padded to a whole number of vectors
    static constexpr auto paddedTaps = [] {
        std::array<int16_t, numPaddedTaps> p{};
        for (size_t i = 0; i < numTaps; i++)
            p[i] = FirDetail::toQ15(taps[i]);
        return p;
    }();

    // room for the history, a chunk of samples, and the zero padded tap tail
    static constexpr size_t WorkSize = FirDetail::ChunkSize + HistorySize + numPaddedTaps;

    alignas(32) int16_t m_historyI[HistorySize > 0 ? HistorySize : 1];
    alignas(32) int16_t m_historyQ[HistorySize > 0 ? HistorySize : 1];
};


template<size_t MaxNumTaps = 64>
class IQLowPassDynamic {
public:
    IQLowPassDynamic()
        :   m_numTaps(1),
            m_areTapsSymmetric(true),
            m_areTapsOdd(true)
    {
        // set all arrays to 0.0f
        std::fill(m_taps.begin(), m_taps.end(), int16_t(0));
        std::fill(m_historyI.begin(), m_historyI.end(), int16_t(0));
        std::fill(m_historyQ.begin(), m_historyQ.end(), int16_t(0));
        // default is instant response pass through, i.e., 1 tap being 1.0f
        m_taps[0] = FirDetail::toQ15(1.0f);
    }

    IQLowPassDynamic(const std::vector<float>& taps)
        :   IQLowPassDynamic()
    {
        setTaps(taps);
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "[IQLowPassDynamic] tap count: " << m_numTaps << " symmetric: " << m_areTapsSymmetric << "\n";
        oss << "[IQLowPassDynamic] taps: (";
        for (size_t i = 0; i < numTaps(); i++) {
            oss << m_taps[i]; //oss << std::bit_cast<uint32_t>(m_taps[i]);
            if ((i + 1) < numTaps()) {
                oss << ", ";
            }
        }
        oss << ")";
        return oss.str();
    }

    void printTabs() {
        std::cerr << "Sym: " << m_areTapsSymmetric << std::endl;
        std::cerr << "Odd: " << m_areTapsOdd << std::endl;
        std::cerr << "Num: " << numTaps() << std::endl;
        for (size_t i = 0; i < numTaps(); i++) {
            std::cerr << m_taps[i] << std::endl;
        }
    }

    bool setTaps(const std::vector<float>& newTaps) {
        if (newTaps.size() == 0)
            return false;

        if (newTaps.size() <= maxNumTaps()) {
            // copy the new values, in Q15, and clear whatever the previous taps left behind
            std::fill(m_taps.begin(), m_taps.end(), int16_t(0));
            for (size_t i = 0; i < newTaps.size(); i++)
                m_taps[i] = FirDetail::toQ15(newTaps[i]);
            // get the new number of tabs
            m_numTaps = newTaps.size();
            m_bufferSize = std::bit_ceil(m_numTaps);
            m_historySize = m_bufferSize - 1;
            // the block filter runs over whole vectors, the padding taps are zero
            m_numPaddedTaps = FirDetail::padTapCount(m_numTaps);
            // check if we have an odd number of taps
            m_areTapsOdd = (m_numTaps % 2) != 0;
            // check if they are symmetric, regardless of if the number is odd or even
            m_areTapsSymmetric = true;
            for (size_t i = 0; i < numTaps() / 2; i++) {
                // compare the opposing elements
                if (m_taps[i] != m_taps[numTaps() - 1 - i]) {
                    // one es enough to break symmetry
                    m_areTapsSymmetric = false;
                    break;
                }
            }
            // the old history was lined up for a different window length
            std::fill(m_historyI.begin(), m_historyI.end(), int16_t(0));
            std::fill(m_historyQ.begin(), m_historyQ.end(), int16_t(0));
            //printTabs();
            return true;
        }
        return false;
    }

    bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        std::vector<float> taps;
        taps.reserve(MaxNumTaps);

        std::string line;
        while (std::getline(file, line)) {
            // trim whitespace
            if (line.empty()) continue;

            // skip comments
            if (line[0] == '#') continue;

            // parse float
            try {
                float v = std::stof(line);
                taps.push_back(v);
            } catch (...) {
                // malformed line
                return false;
            }

            // too many taps
            if (taps.size() > MaxNumTaps) {
                return false;
            }
        }

        // must have at least one tap
        if (taps.empty()) {
            return false;
        }
#if defined(STATS_ENABLED) && STATS_ENABLED
        std::cerr << "[Stream1090] Loaded " << taps.size() << " taps from " << filename << std::endl;
#endif
        return setTaps(taps);
    }


    // returns the maximum number of taps.
    // This is a compile time constant and is 64 by default.
    size_t maxNumTaps() const noexcept {
        return MaxNumTaps;
    }

    // returns the actual number of taps that are in use.
    size_t numTaps() const noexcept {
        return m_numTaps;
    }

    /// Filters a whole block in place.
    void applyBlock(int16_t* __restrict I, int16_t* __restrict Q, size_t n) noexcept {
        alignas(32) int16_t workI[WorkSize];
        alignas(32) int16_t workQ[WorkSize];

        for (size_t base = 0; base < n; base += FirDetail::ChunkSize) {
            const size_t m = std::min(FirDetail::ChunkSize, n - base);

            std::copy(m_historyI.begin(), m_historyI.begin() + m_historySize, workI);
            std::copy(m_historyQ.begin(), m_historyQ.begin() + m_historySize, workQ);
            std::copy(I + base, I + base + m, workI + m_historySize);
            std::copy(Q + base, Q + base + m, workQ + m_historySize);
            // keep the zero taps of the last vector on zeros, not on stack garbage
            std::fill(workI + m_historySize + m, workI + m_historySize + m + m_numPaddedTaps, int16_t(0));
            std::fill(workQ + m_historySize + m, workQ + m_historySize + m + m_numPaddedTaps, int16_t(0));

#if defined(__ARM_NEON)
            if (m_areTapsSymmetric) {
                FirDetail::firBlock<true>(m_taps.data(), m_numTaps,
                                           workI, workQ, I + base, Q + base, m);
            } else {
                FirDetail::firBlock<false>(m_taps.data(), m_numTaps,
                                            workI, workQ, I + base, Q + base, m);
            }
#else
            FirDetail::firBlock<false>(m_taps.data(), m_numTaps,
                                        workI, workQ, I + base, Q + base, m);
#endif

            std::copy(workI + m, workI + m + m_historySize, m_historyI.begin());
            std::copy(workQ + m, workQ + m + m_historySize, m_historyQ.begin());
        }
    }

    // applies the FIR to a single I and Q pair.
    void apply(int16_t& value_I, int16_t& value_Q) noexcept {
        applyBlock(&value_I, &value_Q, 1);
    }

private:
    static constexpr size_t TapCapacity     = FirDetail::padTapCount(MaxNumTaps);
    static constexpr size_t MaxHistorySize  = std::bit_ceil(MaxNumTaps);
    static constexpr size_t WorkSize        = FirDetail::ChunkSize + MaxHistorySize + TapCapacity;

    // the number of taps currently used
    size_t m_numTaps;

    // the same rounded up to a whole number of SIMD vectors
    // firBlock() requires a multiple of four, and the default constructed
    // filter is a one tap pass through, so start at a full group
    size_t m_numPaddedTaps = FirDetail::padTapCount(1);

    // flag indicating if the taps are symmetric
    bool m_areTapsSymmetric;

    // flag indicating if the number of taps is even or odd
    bool m_areTapsOdd;

    // size of the filter window is the smallest power of 2 with >= numTaps
    size_t m_bufferSize = 1;

    // how far back an output reaches
    size_t m_historySize = 0;

    // buffer for the taps, zero padded to a whole vector
    alignas(32) std::array<int16_t, TapCapacity> m_taps;

    // the samples that the next block still needs
    alignas(32) std::array<int16_t, MaxHistorySize> m_historyI;
    alignas(32) std::array<int16_t, MaxHistorySize> m_historyQ;
};
