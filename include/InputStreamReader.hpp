/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <istream>
#include <memory>
#include <unistd.h>

#include "InputReaderBase.hpp"
template<typename RawFormat, size_t InputBufferSize, typename Pipeline>
class InputStdStreamReader : public InputReaderBase<RawFormat, InputBufferSize, Pipeline> {
public:
    using RawType = typename RawFormat::RawType;

    InputStdStreamReader(Pipeline& pipeline, std::istream& stream)
        : InputReaderBase<RawFormat, InputBufferSize, Pipeline>(pipeline),
          m_stream(&stream), m_fd(-1)
    {
        allocateBuffer();
    }

    InputStdStreamReader(Pipeline& pipeline, int fd)
        : InputReaderBase<RawFormat, InputBufferSize, Pipeline>(pipeline),
          m_stream(nullptr), m_fd(fd)
    {
        allocateBuffer();
    }

    inline void readMagnitude(int32_t* out) {
        constexpr size_t N = InputBufferSize;
        constexpr size_t NumValuesToRead = 2 * N;
        constexpr size_t NumBytesToRead  = NumValuesToRead * sizeof(RawType);

        std::streamsize bytesRead = 0;
        if (m_fd >= 0) {
            char* p = reinterpret_cast<char*>(m_buffer.get());
            size_t total = 0;
            while (total < NumBytesToRead) {
                const ssize_t n = ::read(m_fd, p + total, NumBytesToRead - total);
                if (n < 0) {
                    if (errno == EINTR)
                        continue;
                    break;
                }
                if (n == 0)
                    break;
                total += size_t(n);
            }
            bytesRead = std::streamsize(total);
        } else {
            m_stream->read(reinterpret_cast<char*>(m_buffer.get()), NumBytesToRead);
            bytesRead = m_stream->gcount();
        }

        if (bytesRead < std::streamsize(NumBytesToRead)) {
            std::memset(reinterpret_cast<char*>(m_buffer.get()) + bytesRead,
                        0,
                        NumBytesToRead - bytesRead);
            m_eof = true;
        }

        this->processBlock(m_buffer.get(), out);
    }

    bool eof() const {
        return m_eof || ProcessSignals::shutdownRequested();
    }

private:
    void allocateBuffer() {
        constexpr size_t NumValuesToRead = 2 * InputBufferSize;
        m_buffer = std::make_unique<RawType[]>(NumValuesToRead);
        std::fill(m_buffer.get(), m_buffer.get() + NumValuesToRead, RawType(0));
    }

    std::istream* m_stream;
    int m_fd;
    std::unique_ptr<RawType[]> m_buffer;
    bool m_eof = false;
};

