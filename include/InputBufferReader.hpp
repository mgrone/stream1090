/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "InputReaderBase.hpp"
#include "RingBuffer.hpp"

template<typename RawFormat, size_t BufferBlockSize, size_t NumBufferBlocks, typename Pipeline>
class InputBufferReader : public InputReaderBase<RawFormat, BufferBlockSize / 2, Pipeline> {
public:
    using RawType       = typename RawFormat::RawType;
    using RingBufferType = RingBufferAsync<RawType, BufferBlockSize, NumBufferBlocks>;
    using AsyncReader    = typename RingBufferType::Reader;

    InputBufferReader(Pipeline& pipeline, RingBufferType& ringBuffer)
        : InputReaderBase<RawFormat, BufferBlockSize / 2, Pipeline>(pipeline),
          m_reader(ringBuffer)
    { }

    inline void readMagnitude(float* out) {
        m_reader.process([&](const RawType* buffer) {
            this->processBlock(buffer, out);
        });
    }

    bool eof() { 
        return m_reader.eof() || shutdownRequested(); 
    }

private:
    AsyncReader m_reader;
};

