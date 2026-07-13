/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "Global.hpp"
#include "RingBuffer.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <thread>

template<typename InputReader, size_t BlockSize, size_t NumBlocks = 8>
class AsyncMagnitudeReader {
public:
    // inputReader must outlive this object and be unblocked before destruction.
    explicit AsyncMagnitudeReader(InputReader& inputReader)
        : m_inputReader(inputReader),
          m_writer(m_ringBuffer),
          m_reader(m_ringBuffer),
          m_thread([this] { produce(); })
    { }

    ~AsyncMagnitudeReader() {
        m_stopRequested.store(true, std::memory_order_relaxed);
        m_ringBuffer.shutdown();
        if (m_thread.joinable())
            m_thread.join();
    }

    AsyncMagnitudeReader(const AsyncMagnitudeReader&) = delete;
    AsyncMagnitudeReader& operator=(const AsyncMagnitudeReader&) = delete;

    void readMagnitude(float* out) noexcept {
        m_reader.process([&](const float* block) {
            std::memcpy(out, block, BlockSize * sizeof(float));
        });
    }

    bool eof() {
        return ProcessSignals::shutdownRequested() || m_reader.eof();
    }

private:
    void produce() noexcept {
        std::array<float, BlockSize> block;
        while (!m_stopRequested.load(std::memory_order_relaxed) && !m_inputReader.eof()) {
            m_inputReader.readMagnitude(block.data());
            m_writer.write(block.data(), block.size());
        }
        m_writer.shutdown();
    }

    InputReader& m_inputReader;
    RingBufferAsync<float, BlockSize, NumBlocks> m_ringBuffer;
    typename RingBufferAsync<float, BlockSize, NumBlocks>::Writer m_writer;
    typename RingBufferAsync<float, BlockSize, NumBlocks>::Reader m_reader;
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
};
