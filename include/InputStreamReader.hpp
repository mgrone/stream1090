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
#include <functional>
#include <istream>
#include <memory>
#include <poll.h>
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

    /// Optional cancellation for the descriptor path, used when an output can
    /// fail on its own and the run has to end even though stdin never delivers
    /// another byte (a pipe that stays open and idle, or a partial block).
    ///
    /// Without a predicate the read path is exactly what it was: a plain
    /// blocking read(). With one, every read is preceded by a bounded poll(),
    /// so the predicate is re-checked while nothing arrives. It changes no
    /// flag on the shared stdin descriptor and closes nothing.
    void setCancellation(std::function<bool()> cancelled) {
        m_cancelled = std::move(cancelled);
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
                // Checked on every turn of the loop, so a partial block and an
                // EINTR come back here too.
                if (isCancelled())
                    break;
                if (m_cancelled) {
                    pollfd event{m_fd, POLLIN, 0};
                    const int ready = ::poll(&event, 1, CancellationPollMs);
                    if (ready == 0)
                        continue; // nothing to read yet: re-check cancellation
                    if (ready < 0) {
                        if (errno == EINTR)
                            continue;
                        break;
                    }
                }
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
        return m_eof || isCancelled() || ProcessSignals::shutdownRequested();
    }

private:
    // How long a read may wait before the cancellation predicate is consulted
    // again. Short enough that a failed output ends the run promptly, long
    // enough that an idle pipe costs nothing measurable.
    static constexpr int CancellationPollMs = 100;

    bool isCancelled() const {
        return m_cancelled && m_cancelled();
    }

    void allocateBuffer() {
        constexpr size_t NumValuesToRead = 2 * InputBufferSize;
        m_buffer = std::make_unique<RawType[]>(NumValuesToRead);
        std::fill(m_buffer.get(), m_buffer.get() + NumValuesToRead, RawType(0));
    }

    std::istream* m_stream;
    int m_fd;
    std::unique_ptr<RawType[]> m_buffer;
    std::function<bool()> m_cancelled;
    bool m_eof = false;
};

