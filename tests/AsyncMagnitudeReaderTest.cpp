/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "AsyncMagnitudeReader.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <semaphore>
#include <utility>
#include <vector>

namespace {

constexpr size_t BlockSize = 4;
using Block = std::array<float, BlockSize>;

class FiniteReader {
public:
    explicit FiniteReader(std::vector<Block> blocks)
        : m_blocks(std::move(blocks))
    { }

    bool eof() const {
        return m_next == m_blocks.size();
    }

    void readMagnitude(float* out) {
        std::copy(m_blocks[m_next].begin(), m_blocks[m_next].end(), out);
        ++m_next;
    }

private:
    std::vector<Block> m_blocks;
    size_t m_next = 0;
};

class InfiniteReader {
public:
    bool eof() const { return false; }

    void readMagnitude(float* out) {
        std::fill_n(out, BlockSize, 1.0f);
        if (++m_reads == 3)
            m_bufferFilled.release();
    }

    bool waitUntilBufferFilled(std::chrono::milliseconds timeout) {
        return m_bufferFilled.try_acquire_for(timeout);
    }

private:
    size_t m_reads = 0;
    std::binary_semaphore m_bufferFilled{0};
};

class BlockingReader {
public:
    bool eof() {
        m_waiting.release();
        m_stop.acquire();
        return true;
    }

    void readMagnitude(float*) { }

    bool waitUntilBlocked(std::chrono::milliseconds timeout) {
        return m_waiting.try_acquire_for(timeout);
    }

    void stop() {
        m_stop.release();
    }

private:
    std::binary_semaphore m_waiting{0};
    std::binary_semaphore m_stop{0};
};

bool preservesBlockOrder() {
    const std::vector<Block> expected{
        Block{1.0f, 2.0f, 3.0f, 4.0f},
        Block{5.0f, 6.0f, 7.0f, 8.0f},
        Block{9.0f, 10.0f, 11.0f, 12.0f}
    };
    FiniteReader input(expected);
    AsyncMagnitudeReader<FiniteReader, BlockSize, 2> reader(input);

    std::vector<Block> actual;
    while (!reader.eof()) {
        Block block;
        reader.readMagnitude(block.data());
        actual.push_back(block);
    }

    return actual == expected;
}

bool shutdownUnblocksFullOutputBuffer() {
    InfiniteReader input;
    bool bufferFilled = false;
    {
        AsyncMagnitudeReader<InfiniteReader, BlockSize, 2> reader(input);
        bufferFilled = input.waitUntilBufferFilled(std::chrono::seconds(1));
    }
    return bufferFilled;
}

bool stoppingInputUnblocksReader() {
    BlockingReader input;
    bool readerBlocked = false;
    {
        AsyncMagnitudeReader<BlockingReader, BlockSize, 2> reader(input);
        readerBlocked = input.waitUntilBlocked(std::chrono::seconds(1));
        input.stop();
    }
    return readerBlocked;
}

} // namespace

int main() {
    if (!preservesBlockOrder()) {
        std::cerr << "async reader changed block order\n";
        return 1;
    }
    if (!shutdownUnblocksFullOutputBuffer()) {
        std::cerr << "producer did not fill the output buffer\n";
        return 1;
    }
    if (!stoppingInputUnblocksReader()) {
        std::cerr << "producer did not block while waiting for input\n";
        return 1;
    }
    return 0;
}
