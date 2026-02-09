/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "InputReaderBase.hpp"

template<typename T, size_t InputBufferSize, typename Pipeline>
class InputStdStreamReader : public InputReaderBase<T, InputBufferSize, Pipeline> {
public:
    InputStdStreamReader(Pipeline& pipeline, std::istream& stream)
        : InputReaderBase<T, InputBufferSize, Pipeline>(pipeline),
          m_stream(stream)
    {
        constexpr size_t NumValuesToRead = 2 * InputBufferSize;
        m_buffer = std::make_unique<T[]>(NumValuesToRead);
        std::fill(m_buffer.get(), m_buffer.get() + NumValuesToRead, T(0));
    }

    inline void readMagnitude(float* out) {
        constexpr size_t N = InputBufferSize;
        constexpr size_t NumValuesToRead = 2 * N;
        constexpr size_t NumBytesToRead = NumValuesToRead * sizeof(T);

        // Read from the istream BYTES. Note that it may happen
        // that in case of a file we may have reached the end and 
        // are not able to read the full batch
        m_stream.read(reinterpret_cast<char*>(m_buffer.get()), NumBytesToRead);
        std::streamsize bytesRead = m_stream.gcount();

        // If we did not manage to read a full set of values, we will zero the
        // remaining ones. This is hacky, but the subsequent stages require a full
        // set, and currently there are old values there, which may contain messages
        // from the previous iteration. 
        if (bytesRead < std::streamsize(NumBytesToRead)) {
            // So we fix that here in a dirty way to avoid duplicate
            // messages at the end of a test sample file
            std::memset(reinterpret_cast<char*>(m_buffer.get()) + bytesRead, 0, NumBytesToRead - bytesRead);
            // however, we do not need another iteration
            m_eof = true;
        }

        // Send them down the pipeline
        this->processBlock(m_buffer.get(), out);
    }

    bool eof() const { 
        return m_eof || shutdownRequested(); 
    }

private:
    // the input stream to read from
    std::istream& m_stream;

    // a temporary buffer
    std::unique_ptr<T[]> m_buffer;
    
    // flag for end of file    
    bool m_eof = false;
};

