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

    template<size_t RatioInput, size_t RatioOutput>
    constexpr auto makeLinearInterpTable() {
        std::array<float, RatioOutput> alpha{};
        std::array<size_t, RatioOutput> k{};

        for (size_t j = 0; j < RatioOutput; j++) {
            const float t = float(j) * float(RatioInput) / float(RatioOutput);
            const size_t ki = size_t(t);
            const float a = t - float(ki);

            k[j] = ki;
            alpha[j] = a;
        }

        return std::pair{k, alpha};
    }

} // end of namespace

template<size_t RatioInput, size_t RatioOutput, size_t NumBlocks>
struct SamplerFunc {

    static constexpr auto tbl = SamplerFunc_details::makeLinearInterpTable<RatioInput, RatioOutput>();
    static constexpr auto& k = tbl.first;
    static constexpr auto& a = tbl.second;

    static constexpr void sample(const float* __restrict in,
                                 float* __restrict out) noexcept
    {
        for (size_t i = 0; i < NumBlocks; i++) {
            for (size_t j = 0; j < RatioOutput; j++) {
                const float alpha = a[j];
                const float l = 1.0f - alpha;
                const float r = alpha;
                out[j] = l * in[k[j]] + r * in[k[j] + 1];
            }
            in  += RatioInput;
            out += RatioOutput;
        }
    }
};
