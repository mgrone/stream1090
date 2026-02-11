/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <string>

template<typename RawFormat, size_t InputBufferSize, typename Pipeline>
class InputReaderBase {
public:
    using RawType = typename RawFormat::RawType;

    InputReaderBase(Pipeline& pipeline) noexcept
        : m_pipeline(pipeline) {}

    inline void processBlock(const RawType* __restrict in,
                             float* __restrict out) noexcept {
        constexpr size_t N = InputBufferSize;
        for (size_t i = 0; i < N; ++i) {
            float I = RawFormat::convertScalar(*in++);
            float Q = RawFormat::convertScalar(*in++);
            *out++ = m_pipeline.process(I, Q);
        }
    }

private:
    Pipeline& m_pipeline;
};
