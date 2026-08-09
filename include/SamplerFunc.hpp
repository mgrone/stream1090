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

#ifndef STREAM1090_INTERP
#define STREAM1090_INTERP 1
#endif

#if STREAM1090_INTERP != 1 && STREAM1090_INTERP != 6
#error "STREAM1090_INTERP must be 1 (linear) or 6 (sharpening cubic)"
#endif

namespace SamplerFunc_details {

    // interpolation weight in Q16, so the generic path stays integer
    inline constexpr int WeightFracBits = 16;

    constexpr int32_t toFixedWeight(double value) noexcept {
        const double scaled = value * double(1 << WeightFracBits);
        return int32_t(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
    }

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

    constexpr double mitchell(double x, double b, double c) noexcept {
        if (x < 0.0)
            x = -x;
        const double x2 = x * x;
        const double x3 = x2 * x;
        if (x < 1.0)
            return ((12.0 - 9.0 * b - 6.0 * c) * x3
                  + (-18.0 + 12.0 * b + 6.0 * c) * x2
                  + (6.0 - 2.0 * b)) / 6.0;
        if (x < 2.0)
            return ((-b - 6.0 * c) * x3
                  + (6.0 * b + 30.0 * c) * x2
                  + (-12.0 * b - 48.0 * c) * x
                  + (8.0 * b + 24.0 * c)) / 6.0;
        return 0.0;
    }

    template<size_t RatioInput, size_t RatioOutput>
    constexpr auto makeSharpeningCubicTable() {
        constexpr double b = 0.0;
        constexpr double c = 1.4;
        std::array<std::array<int32_t, 4>, RatioOutput> weights{};
        for (size_t j = 0; j < RatioOutput; ++j) {
            const double t = double(j) * double(RatioInput) / double(RatioOutput);
            const double alpha = t - double(size_t(t));
            weights[j][0] = toFixedWeight(mitchell(alpha + 1.0, b, c));
            weights[j][1] = toFixedWeight(mitchell(alpha,       b, c));
            weights[j][2] = toFixedWeight(mitchell(1.0 - alpha, b, c));
            weights[j][3] = toFixedWeight(mitchell(2.0 - alpha, b, c));
        }
        return weights;
    }

} // end of namespace

template<size_t RatioInput, size_t RatioOutput, size_t NumBlocks>
struct SamplerFunc {

    static constexpr bool UseSharpeningCubic =
        STREAM1090_INTERP == 6
        && ((RatioInput == 1 && RatioOutput == 5)
            || (RatioInput == 8 && RatioOutput == 25)
            || (RatioInput == 16 && RatioOutput == 75));

    static constexpr auto tbl = SamplerFunc_details::makeLinearInterpTable<RatioInput, RatioOutput>();
    static constexpr auto& k = tbl.first;
    static constexpr auto& a = tbl.second;

    static constexpr void sampleLinearBlock(const int32_t* __restrict in,
                                            int32_t* __restrict out) noexcept
    {
        using namespace SamplerFunc_details;
        for (size_t j = 0; j < RatioOutput; ++j) {
            const int32_t r = a[j];
            const int32_t l = (1 << WeightFracBits) - r;
            const int64_t acc = int64_t(l) * int64_t(in[k[j]])
                              + int64_t(r) * int64_t(in[k[j] + 1]);
            out[j] = int32_t(acc >> WeightFracBits);
        }
    }

#if STREAM1090_INTERP == 6
    static constexpr auto weights =
        SamplerFunc_details::makeSharpeningCubicTable<RatioInput, RatioOutput>();
#endif

    static constexpr void sample(const int32_t* __restrict in,
                                 int32_t* __restrict out) noexcept
    {
#if STREAM1090_INTERP == 6
        if constexpr (UseSharpeningCubic) {
            sampleLinearBlock(in, out);
            in  += RatioInput;
            out += RatioOutput;
            for (size_t i = 1; i + 1 < NumBlocks; ++i) {
                for (size_t j = 0; j < RatioOutput; ++j) {
                    const int32_t* p = in + k[j];
                    const int64_t acc = int64_t(weights[j][0]) * int64_t(p[-1])
                                      + int64_t(weights[j][1]) * int64_t(p[0])
                                      + int64_t(weights[j][2]) * int64_t(p[1])
                                      + int64_t(weights[j][3]) * int64_t(p[2]);
                    out[j] = int32_t(acc >> SamplerFunc_details::WeightFracBits);
                }
                in  += RatioInput;
                out += RatioOutput;
            }
            sampleLinearBlock(in, out);
        } else {
#endif
            for (size_t i = 0; i < NumBlocks; i++) {
                sampleLinearBlock(in, out);
                in  += RatioInput;
                out += RatioOutput;
            }
#if STREAM1090_INTERP == 6
        }
#endif
    }
};
