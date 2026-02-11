/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "InputReaderBase.hpp"
template<typename RawFormat, size_t InputBufferSize, typename Pipeline>
class InputStdStreamReader : public InputReaderBase<RawFormat, InputBufferSize, Pipeline> {
public:
    using RawType = typename RawFormat::RawType;

    InputStdStreamReader(Pipeline& pipeline, std::istream& stream)
        : InputReaderBase<RawFormat, InputBufferSize, Pipeline>(pipeline),
          m_stream(stream)
    {
        constexpr size_t NumValuesToRead = 2 * InputBufferSize;
        m_buffer = std::make_unique<RawType[]>(NumValuesToRead);
        std::fill(m_buffer.get(), m_buffer.get() + NumValuesToRead, RawType(0));
    }

    inline void readMagnitude(float* out) {
        constexpr size_t N = InputBufferSize;
        constexpr size_t NumValuesToRead = 2 * N;
        constexpr size_t NumBytesToRead  = NumValuesToRead * sizeof(RawType);

        m_stream.read(reinterpret_cast<char*>(m_buffer.get()), NumBytesToRead);
        std::streamsize bytesRead = m_stream.gcount();

        if (bytesRead < std::streamsize(NumBytesToRead)) {
            std::memset(reinterpret_cast<char*>(m_buffer.get()) + bytesRead,
                        0,
                        NumBytesToRead - bytesRead);
            m_eof = true;
        }

        this->processBlock(m_buffer.get(), out);
    }

    bool eof() const {
        return m_eof || shutdownRequested();
    }

private:
    std::istream& m_stream;
    std::unique_ptr<RawType[]> m_buffer;
    bool m_eof = false;
};


