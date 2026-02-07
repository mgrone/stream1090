/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <string>

namespace rawToFloat {

    template<typename T>
    inline float convert(T v) = delete;

    template<>
    inline float convert<uint8_t>(uint8_t v) {
        return (float(v) - 127.5f) * (1.0f / 127.5f);
    }

    template<>
    inline float convert<uint16_t>(uint16_t v) {
        return (float(v) - 2047.5f) * (1.0f / 2047.5f);
    }

    template<>
    inline float convert<float>(float v) {
        return v;
    }
}

template<typename T, size_t InputBufferSize, typename Pipeline>
class InputReaderBase {
public:
    InputReaderBase(Pipeline& pipeline)
        : m_pipeline(pipeline) { }

    inline void processBlock(const T* __restrict in, float* __restrict out) {
        constexpr size_t N = InputBufferSize;
        for (size_t i = 0; i < N; ++i) {
            float I = rawToFloat::convert(*in++);
            float Q = rawToFloat::convert(*in++);
            *out++ = m_pipeline.process(I, Q);
        }
    }

private:
    Pipeline& m_pipeline;
};
