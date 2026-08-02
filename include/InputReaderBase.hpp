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
     * pass 3 becomes a vector sqrt. In the fused per sample version neither
     * could vectorize, because the stateful stage sat between them.
     *
     * The work is tiled so the two scratch arrays stay in L1 no matter how
     * large the input buffer is.
     */
    inline void processBlock(const RawType* __restrict in,
                             float* __restrict out) noexcept {
        constexpr size_t N = InputBufferSize;

        for (size_t base = 0; base < N; base += TileSize) {
            const size_t n = std::min(TileSize, N - base);
            const RawType* __restrict raw = in + 2 * base;

            for (size_t i = 0; i < n; i++) {
                m_tileI[i] = RawFormat::convertScalar(raw[2 * i]);
                m_tileQ[i] = RawFormat::convertScalar(raw[2 * i + 1]);
            }

            if constexpr (!PipelineType::isEmpty) {
                m_pipeline.applyStagesBlock(m_tileI, m_tileQ, n);
            }

            for (size_t i = 0; i < n; i++) {
                out[base + i] = std::sqrt(m_tileI[i] * m_tileI[i] + m_tileQ[i] * m_tileQ[i]);
            }
        }
    }

private:
    // callers hand in the pipeline as a reference type
    using PipelineType = std::remove_reference_t<Pipeline>;

    static constexpr size_t TileSize = (InputBufferSize < 512) ? InputBufferSize : 512;

    alignas(32) float m_tileI[TileSize];
    alignas(32) float m_tileQ[TileSize];

    Pipeline& m_pipeline;
};
