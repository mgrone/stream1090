/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "InputReaderBase.hpp"
#include "RingBuffer.hpp"

template<typename T, size_t BufferBlockSize, size_t NumBufferBlocks, typename Pipeline>
class InputBufferReader : public InputReaderBase<T, BufferBlockSize / 2, Pipeline> {
public:
    using RingBufferType = RingBufferAsync<T, BufferBlockSize, NumBufferBlocks>;
    using AsyncReader = RingBufferType::Reader;

    InputBufferReader(Pipeline& pipeline, RingBufferType& ringBuffer)
        : InputReaderBase<T, BufferBlockSize / 2, Pipeline>(pipeline), m_reader(ringBuffer)
    { }

    inline void readMagnitude(float* out) {
        // call the reader to get the next buffer of input from
        // the ring buffer. This function will block if no new 
        // block of size InputSize is available. 
        m_reader.process([&](const T* buffer) {
            // Send them down the pipeline
            this->processBlock(buffer, out);
        });
    }

    bool eof() { 
        return m_reader.eof() || shutdownRequested(); 
    }

private:
    // the reader which will read from a ringbuffer
    AsyncReader m_reader;
};
