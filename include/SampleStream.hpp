/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <cstring>
#include <algorithm>

#include "DemodCore.hpp"
#include "Sampler.hpp"
#include "MessageHandler.hpp"

// ------------------------------------------------------------
// SPSC BlockRing
// ------------------------------------------------------------
template<typename T, size_t BlockSize, size_t NumBlocks, size_t Delay = 0, size_t NumHistoryBlocks = 0>
class BlockRingAsync {
public:
    static constexpr size_t TotalSize = BlockSize * NumBlocks + Delay;

    BlockRingAsync(const T& initValue)
        : m_data(
            new (std::align_val_t(16)) T[TotalSize],
            AlignedDeleter{}
        ),
          m_readPos(0),
          m_writePos(Delay),
          m_fullBlocks(0),
          m_finished(false)
    {
        std::fill(m_data.get(), m_data.get() + TotalSize, initValue);
    }

    // producer thread
    T* writePos() noexcept {
        return m_data.get() + m_writePos.load(std::memory_order_relaxed);
    }

    // producer thread
    void advanceWritePos() noexcept {
        size_t wp = m_writePos.load(std::memory_order_relaxed);
        wp += BlockSize;

        if (wp + BlockSize > TotalSize) {
            if constexpr (Delay > 0) {
                std::memcpy(
                    m_data.get(),
                    m_data.get() + (TotalSize - Delay),
                    Delay * sizeof(T)
                );
            }
            wp = Delay;
        }

        m_writePos.store(wp, std::memory_order_release);
        m_fullBlocks.fetch_add(1, std::memory_order_release);
    }

    // consumer thread
    const T* readPos() const noexcept {
        return m_data.get() + m_readPos.load(std::memory_order_relaxed);
    }

    // consumer thread
    void advanceReadPos() noexcept {
        size_t rp = m_readPos.load(std::memory_order_relaxed);
        rp += BlockSize;

        if (rp >= BlockSize * NumBlocks) {
            rp = 0;
        }

        m_readPos.store(rp, std::memory_order_release);
        m_fullBlocks.fetch_sub(1, std::memory_order_release);
    }

    // consumer thread
    bool isReadable() const noexcept {
        return m_fullBlocks.load(std::memory_order_acquire) > 0;
    }

    // producer thread
    bool isWritable() const noexcept {
        return m_fullBlocks.load(std::memory_order_acquire) < NumBlocks - 1;
    }

    // consumer thread
    const T& lookBack(size_t k, size_t offsetInBlock = 0) const noexcept {
        size_t rp = m_readPos.load(std::memory_order_relaxed);
        size_t absoluteIndex = rp + offsetInBlock;
        if (absoluteIndex >= TotalSize)
            absoluteIndex -= TotalSize;

        size_t lookBackIndex = absoluteIndex + TotalSize - k;
        if (lookBackIndex >= TotalSize)
            lookBackIndex -= TotalSize;

        return m_data[lookBackIndex];
    }

    // consumer thread
    bool eof() const noexcept {
        return m_finished.load(std::memory_order_acquire) &&
               (m_fullBlocks.load(std::memory_order_acquire) == 0);
    }

    // producer thread
    void finished() noexcept {
        m_finished.store(true, std::memory_order_release);
    }

private:
    struct AlignedDeleter {
        void operator()(T* p) const noexcept {
            ::operator delete[](p, std::align_val_t(16));
        }
    };

    std::unique_ptr<T[], AlignedDeleter> m_data;

    std::atomic<size_t> m_readPos;
    std::atomic<size_t> m_writePos;
    std::atomic<size_t> m_fullBlocks;
    std::atomic<bool>   m_finished;
};

// the main stream class. This class manages reading from the input stream
// and also manages the buffers
template<typename Sampler>
class SampleStream {
public:
    static constexpr size_t NumInputBuffers = 4;
    // for now we will keep one extra sample buffer as history
    static constexpr size_t NumSampleBuffers = 5;
    static constexpr size_t TotalSampleBufferLength = NumSampleBuffers * Sampler::SampleBufferSize + Sampler::SampleBufferOverlap;

    SampleStream() : m_inputRingBuffer(0.0f), m_sampleRingBuffer(0.0f) { }
   
    // the main method that streams from InputStream using inputReader
    template<typename InputReaderType, MessageHandler Handler>
    void read(InputReaderType& inputReader, Handler& messageHandler);

    uint8_t getRSSI() const noexcept {
        // we are 128 bits behind and are looking for the preamble pulse
        constexpr size_t bitDelay     = 128 - 8;
        // how much is that in samples?
        constexpr size_t samplesDelay = bitDelay * Sampler::NumStreams;
        // how far is the demodulator in the block?
        const auto offsetInBlock = m_demodPos - m_sampleRingBuffer.readPos();
        // check the rssi of the surounding samples. This index is the first to
        // catch the message, usually with bad RSSI
        float rssi = 0.0f;
        for (size_t s = 0; s < Sampler::NumStreams; s++) {
            float v = std::max(m_sampleRingBuffer.lookBack(samplesDelay + s, offsetInBlock), 
                               m_sampleRingBuffer.lookBack(samplesDelay + s - (Sampler::NumStreams >> 1), offsetInBlock));
            rssi = std::max(rssi, v);
        }
        // normalize
        rssi = std::min(1.41f, rssi) / 1.41f;
        // and return as byte
        return uint8_t(rssi * 255.0);
    }

private:
    uint32_t m_newBits[Sampler::NumStreams];    
    // we have one ring buffer for the IQ pipeline
    BlockRingAsync<float, Sampler::InputBufferSize,  NumInputBuffers,  Sampler::InputBufferOverlap>  m_inputRingBuffer;
    // and one for the upsampled magnitudes
    BlockRingAsync<float, Sampler::SampleBufferSize, NumSampleBuffers, Sampler::SampleBufferOverlap> m_sampleRingBuffer;
    // not nice. Will change
    const float* m_demodPos = nullptr;
};


template<typename Sampler>
template<typename InputReaderType, MessageHandler Handler>
inline void SampleStream<Sampler>::read(InputReaderType& inputReader, Handler& messageHandler) {  
    DemodCore<Sampler::NumStreams, Handler> demodCore(messageHandler);

    std::thread helperThreadA([&]{ 
        while (!inputReader.eof()) {
            // check writable
            if (!m_inputRingBuffer.isWritable()) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                continue;
            }

            inputReader.readMagnitude(m_inputRingBuffer.writePos());
            m_inputRingBuffer.advanceWritePos();
        }
        //std::cerr << "m_inputRingBuffer finished" << std::endl;
        m_inputRingBuffer.finished();
    });

    std::thread helperThreadB([&]{ 
        while (!m_inputRingBuffer.eof()) {
            // check writable
            if (!m_sampleRingBuffer.isWritable() || !m_inputRingBuffer.isReadable()) {
                std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                continue;
            }

            Sampler::sample(m_inputRingBuffer.readPos(),
                            m_sampleRingBuffer.writePos());

            m_inputRingBuffer.advanceReadPos();
            m_sampleRingBuffer.advanceWritePos();
        }
        //std::cerr << "m_sampleRingBuffer" << std::endl;
        m_sampleRingBuffer.finished();
    });
        
    while (!m_sampleRingBuffer.eof()) {

        if (!m_sampleRingBuffer.isReadable()) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            std::this_thread::yield();
            continue;
        }

        m_demodPos = m_sampleRingBuffer.readPos();

        for (size_t i = 0; i < Sampler::SampleBufferSize; i += Sampler::NumStreams) {
            for (size_t j = 0; j < Sampler::NumStreams; j++) {
                m_newBits[j] = m_demodPos[j] > m_demodPos[j + (Sampler::NumStreams >> 1)];
            }
            demodCore.shiftInNewBits(m_newBits);
            m_demodPos += Sampler::NumStreams;
        }

        m_sampleRingBuffer.advanceReadPos();
    }

    helperThreadA.join();
    helperThreadB.join();
}
