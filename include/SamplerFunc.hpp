/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once
#include <numeric>
#include <cstddef>
#include <array>

namespace SamplerFunc_details {

template<size_t RatioInput, size_t RatioOutput, bool firstCoef = true> 
constexpr auto makeCoefArray() {
    std::array<float, RatioOutput> res;
    constexpr auto numIn = RatioInput;
    constexpr auto numOut = RatioOutput;
    for (size_t j = 0; j < numOut; j++) {
        const auto offset = numIn * j;
        if constexpr(firstCoef) {
            res[j] = float (RatioOutput - (offset % numOut));
        } else {
            res[j] = float ((offset % numOut));
        }
    }
    return res;
} 

template<size_t RatioInput, size_t RatioOutput> 
constexpr auto makeOffsetArray() {
    std::array<size_t, RatioOutput> res;
    constexpr auto num_in = RatioInput;
    constexpr auto num_out = RatioOutput;
    for (size_t j = 0; j < num_out; j++) {
        const auto offset = num_in * j;
        res[j] = offset / num_out;
    }
    return res;
} 

template<size_t RatioInput, size_t RatioOutput>
struct CoefHelper {
    static constexpr auto first   = makeCoefArray<RatioInput, RatioOutput, true>();
    static constexpr auto second  = makeCoefArray<RatioInput, RatioOutput, false>();
    static constexpr auto offset  = makeOffsetArray<RatioInput, RatioOutput>();
};

} // end of namespace

template<size_t RatioInput, size_t RatioOutput, size_t NumBlocks>
struct SamplerFunc {

    // we will use a helper struct for precomputing coefficients
    using C = SamplerFunc_details::CoefHelper<RatioInput, RatioOutput>;
    constexpr static auto numBlocks = NumBlocks;
    constexpr static auto numIn = RatioInput;
    constexpr static auto numOut = RatioOutput;

    // make sure that we really upsample
    static_assert(RatioInput < RatioOutput);

    static constexpr void sample(float* __restrict in, float* __restrict out) noexcept {
        // we could move this into the precomputed coefficients.
        // Sadly this is then not the same anymore
        constexpr float scale = (1.0f / (float(numOut)));

        // for all blocks, take numIn many input samples and produce numOut many output samples
        for (size_t i = 0; i < numBlocks; i++) {
            // we just go through all output samples
            for (size_t j = 0; j < numOut; j++) {
                // get the offset of the two consecutive input samples
                const auto k = C::offset[j];
                // multiply them with the coefs + scaling
                out[j] = ((C::first[j]) * in[k] + (C::second[j]) * in[k+1]) * scale;
            }

            // move on to the next block
            in += numIn;
            out += numOut;
        }
    }
};

