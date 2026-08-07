/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <string>
#include <cmath>
#include <algorithm>
#include <type_traits>

template<typename RawFormat, size_t InputBufferSize, typename Pipeline>
class InputReaderBase {
public:
    using RawType = typename RawFormat::RawType;

    InputReaderBase(Pipeline& pipeline) noexcept
        : m_pipeline(pipeline) {}

    /*
     * The samples make three short passes instead of one long one:
     *
     *   1. split the interleaved raw stream into an I and a Q array
     *   2. run the pipeline stages, which carry state and stay scalar
     *   3. turn I/Q into the magnitude
     *
     * Passes 1 and 3 have no loop carried dependency, so they vectorize. On
     * ARM pass 1 becomes a deinterleaving load plus a widening convert, and
     * pass 3 becomes a vector square and add. In the fused per sample version neither
     * could vectorize, because the stateful stage sat between them.
     *
     * The work is tiled so the two scratch arrays stay in L1 no matter how
     * large the input buffer is.
     */
    inline void processBlock(const RawType* __restrict in,
                             int32_t* __restrict out) noexcept {
        constexpr size_t N = InputBufferSize;

        for (size_t base = 0; base < N; base += TileSize) {
            const size_t n = std::min(TileSize, N - base);
            const RawType* __restrict raw = in + 2 * base;

            for (size_t i = 0; i < n; i++) {
                m_tileI[i] = RawFormat::convertFixed(raw[2 * i]);
                m_tileQ[i] = RawFormat::convertFixed(raw[2 * i + 1]);
            }

            if constexpr (!PipelineType::isEmpty) {
                m_pipeline.applyStagesBlock(m_tileI, m_tileQ, n);
            }

            for (size_t i = 0; i < n; i++) {
                    // Linear magnitude, as before, but the samples are integers
                    // now. Squaring the ring instead would be cheaper, and the
                    // square root is monotone so it cannot flip a comparison --
                    // but the resampler interpolates this ring, and interpolation
                    // does not commute with squaring, which costs about 0.5% of
                    // messages on a busy 2.4 Msps feed. Measured, so kept linear.
                    const int32_t i2 = int32_t(m_tileI[i]) * int32_t(m_tileI[i]);
                    const int32_t q2 = int32_t(m_tileQ[i]) * int32_t(m_tileQ[i]);
                    // 8 fractional bits kept: the root of a weak sample is only
                    // a couple of hundred, and truncating it to an integer costs
                    // about 0.3% of messages on its own.
                    out[base + i] = int32_t(std::sqrt(float(i2 + q2)) * 256.0f + 0.5f);
            }
        }
    }

private:
    // callers hand in the pipeline as a reference type
    using PipelineType = std::remove_reference_t<Pipeline>;

    static constexpr size_t TileSize = (InputBufferSize < 512) ? InputBufferSize : 512;

    alignas(32) int16_t m_tileI[TileSize];
    alignas(32) int16_t m_tileQ[TileSize];

    Pipeline& m_pipeline;
};
