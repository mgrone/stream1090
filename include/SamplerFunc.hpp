/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once
#include <numeric>
#include <cstddef>
#include <cstdint>
#include <array>

namespace SamplerFunc_details {

    // interpolation weight in Q16, so the generic path stays integer
    inline constexpr int WeightFracBits = 16;

    template<size_t RatioInput, size_t RatioOutput>
    constexpr auto makeLinearInterpTable() {
        std::array<int32_t, RatioOutput> alpha{};
        std::array<size_t, RatioOutput> k{};

        for (size_t j = 0; j < RatioOutput; j++) {
            const double t = double(j) * double(RatioInput) / double(RatioOutput);
            const size_t ki = size_t(t);
            const double a = t - double(ki);

            k[j] = ki;
            alpha[j] = int32_t(a * double(1 << WeightFracBits) + 0.5);
        }

        return std::pair{k, alpha};
    }

} // end of namespace

template<size_t RatioInput, size_t RatioOutput, size_t NumBlocks>
struct SamplerFunc {

    static constexpr auto tbl = SamplerFunc_details::makeLinearInterpTable<RatioInput, RatioOutput>();
    static constexpr auto& k = tbl.first;
    static constexpr auto& a = tbl.second;

    static constexpr void sample(const int32_t* __restrict in,
                                 int32_t* __restrict out) noexcept
    {
        using namespace SamplerFunc_details;
        for (size_t i = 0; i < NumBlocks; i++) {
            for (size_t j = 0; j < RatioOutput; j++) {
                const int32_t r = a[j];
                const int32_t l = (1 << WeightFracBits) - r;
                // the samples carry 28 bits, so the weighted sum is widened
                const int64_t acc = int64_t(l) * int64_t(in[k[j]])
                                  + int64_t(r) * int64_t(in[k[j] + 1]);
                out[j] = int32_t(acc >> WeightFracBits);
            }
            in  += RatioInput;
            out += RatioOutput;
        }
    }
};
