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
#include <cmath>

#include "RawInputFormat.hpp"
#include "DemodCore.hpp"
#include "Sampler.hpp"
#include "MessageHandler.hpp"

#pragma once
#include <memory>
#include <cstring>
#include <algorithm>

template<typename T, size_t BlockSize, size_t NumBlocks, size_t Delay = 0>
class BlockRing {
public:
    static constexpr size_t TotalSize = BlockSize * NumBlocks + Delay;

    BlockRing(const T& initValue)
        : m_data(
            new (std::align_val_t(16)) T[TotalSize],   // aligned allocation
            AlignedDeleter{}                           // matching deleter
        ),
          m_readPos(0),
          m_writePos(Delay),
          m_fullBlocks(0)
    {
        std::fill(m_data.get(), m_data.get() + TotalSize, initValue);
    }

    T* writePos() noexcept {
        return m_data.get() + m_writePos;
    }

    void advanceWritePos() noexcept {
        m_writePos += BlockSize;

        if (m_writePos + BlockSize > TotalSize) {
            if constexpr (Delay > 0) {
                std::memcpy(
                    m_data.get(),
                    m_data.get() + (TotalSize - Delay),
                    Delay * sizeof(T)
                );
            }
            m_writePos = Delay;
        }

        m_fullBlocks++;
    }

    const T* readPos() const noexcept {
        return m_data.get() + m_readPos;
    }

    void advanceReadPos() noexcept {
        m_readPos += BlockSize;

        if (m_readPos >= BlockSize * NumBlocks) {
            m_readPos = 0;
        }

        m_fullBlocks--;
    }

    bool isReadable() const noexcept {
        return m_fullBlocks > 0;
    }

    bool isWritable() const noexcept {
        return m_fullBlocks < NumBlocks;
    }

    const T& lookBack(size_t k, size_t offsetInBlock = 0) const noexcept {
        size_t absoluteIndex = m_readPos + offsetInBlock;
        if (absoluteIndex >= TotalSize)
            absoluteIndex -= TotalSize;

        size_t lookBackIndex = absoluteIndex + TotalSize - k;
        if (lookBackIndex >= TotalSize)
            lookBackIndex -= TotalSize;

        return m_data[lookBackIndex];
    }

private:
    struct AlignedDeleter {
        void operator()(T* p) const noexcept {
            ::operator delete[](p, std::align_val_t(16));
        }
    };

    std::unique_ptr<T[], AlignedDeleter> m_data;

    size_t m_readPos;
    size_t m_writePos;
    size_t m_fullBlocks;
};



// the main stream class. This class manages reading from the input stream
// and also manages the buffers
template<typename Sampler>
class SampleStream {
public:
    static constexpr size_t NumInputBuffers = 2;
    // for now we will keep one extra sample buffer as history
    static constexpr size_t NumSampleBuffers = 2;
    static constexpr size_t TotalSampleBufferLength = NumSampleBuffers * Sampler::SampleBufferSize + Sampler::SampleBufferOverlap;

    SampleStream() : m_inputRingBuffer(0), m_sampleRingBuffer(0) { }
   
    // the main method that streams from InputStream using inputReader
    template<typename InputReaderType, MessageHandler Handler>
    void read(InputReaderType& inputReader, Handler& messageHandler);

    /// Recompute the demodulator's confidence in a single frame bit, straight
    /// from the retained sample ring. The demodulation loop deliberately stores
    /// nothing: this is only ever called for a frame that already matched a
    /// trusted address, a few dozen times a second, so recomputing is far
    /// cheaper than keeping a per-bit history for every stream.
    float confidenceAt(uint8_t frameBit, size_t stream) const noexcept {
        // frame bit f was shifted in (16 + f) bit periods ago; see
        // ShiftRegisters::extractAlignedFrameLong for the 16 bit alignment
        const size_t delay = (size_t(16) + frameBit) * Sampler::NumStreams;
        const auto offsetInBlock = size_t(m_demodPos - m_sampleRingBuffer.readPos());
        // the ring holds linear magnitudes scaled by MagScale
        const float first  = float(m_sampleRingBuffer.lookBack(delay - stream, offsetInBlock));
        const float second = float(m_sampleRingBuffer.lookBack(delay - stream - (Sampler::NumStreams >> 1),
                                                               offsetInBlock));
        return std::fabs(first - second) * (1.0f / MagScale);
    }

    uint8_t getRSSI() const noexcept {
        // we are 128 bits behind and are looking for the preamble pulse
        constexpr size_t bitDelay     = 128 - 8;
        // how much is that in samples?
        constexpr size_t samplesDelay = bitDelay * Sampler::NumStreams;
        // how far is the demodulator in the block?
        const auto offsetInBlock = m_demodPos - m_sampleRingBuffer.readPos();
        // check the rssi of the surounding samples. This index is the first to
        // catch the message, usually with bad RSSI
        int32_t peak = 0;
        for (size_t s = 0; s < Sampler::NumStreams; s++) {
                int32_t v = std::max(m_sampleRingBuffer.lookBack(samplesDelay + s, offsetInBlock),
                                     m_sampleRingBuffer.lookBack(samplesDelay + s - (Sampler::NumStreams >> 1), offsetInBlock));
                peak = std::max(peak, v);
            }
            float rssi = float(peak) * (1.0f / MagScale);
        // normalize
        rssi = std::min(1.41f, rssi) / 1.41f;
        // and return as byte
        return uint8_t(rssi * 255.0);
    }

private:
    // Samples reach the ring as ((I*I) >> 2) + ((Q*Q) >> 2) with I and Q in
    // Q14, so the stored value is the squared magnitude times (SampleOne/2)^2
    // and a linear magnitude comes back as stored / MagScale.
    static constexpr float MagScale = float(SampleOne) * 256.0f;

    uint32_t m_newBits[Sampler::NumStreams];
    // we have one ring buffer for the IQ pipeline
    BlockRing<int32_t, Sampler::InputBufferSize,  NumInputBuffers,  Sampler::InputBufferOverlap>  m_inputRingBuffer;
    // and one for the upsampled magnitudes
    BlockRing<int32_t, Sampler::SampleBufferSize, NumSampleBuffers, Sampler::SampleBufferOverlap> m_sampleRingBuffer;
    // not nice. Will change
    const int32_t* m_demodPos = nullptr;
};


template<typename Sampler>
template<typename InputReaderType, MessageHandler Handler>
inline void SampleStream<Sampler>::read(InputReaderType& inputReader, Handler& messageHandler) {  
    // the core logic for message recognition
    DemodCore<Sampler::NumStreams, Handler> demodCore(messageHandler);
    demodCore.setConfidenceSource(this, [](const void* ctx, uint8_t bit, size_t stream) -> float {
        return static_cast<const SampleStream<Sampler>*>(ctx)->confidenceAt(bit, stream);
    });

     // the main loop for reading the stream
    while (!inputReader.eof()) {
        // the read and write positions for the current sample buffer based its index.
        // we start reading at 0 + i * size           
        // however, new values will be written NumStream / 2 later which is the overlap. 
        // check if actually we need the sampler to resample, or if this is a 1:1 sampling
        if constexpr(Sampler::isPassthrough) {
            // tell the input reader to get us some data. Directly as magnitude. Since this is a passthrough sampler
            // we will directly read into the samples buffer. There is no need for using the sampler at all.
            // This works because the amount the input reader is getting us in this particular case is exactly the ChunkSize
            static_assert(Sampler::NumBlocks == Sampler::InputBufferSize);
            inputReader.readMagnitude(m_sampleRingBuffer.writePos());
            m_sampleRingBuffer.advanceWritePos();
        } else {
            // tell the input reader to get us some data. Directly as magnitude.
            inputReader.readMagnitude(m_inputRingBuffer.writePos());
            m_inputRingBuffer.advanceWritePos();
            // now ask the Sampler to resample the input magnitude to the output samples
            // similar to the input buffer, write after the overlap to keep some old values for the next iteration
            if (m_inputRingBuffer.isReadable()) {
                Sampler::sample(m_inputRingBuffer.readPos(), m_sampleRingBuffer.writePos());
                m_inputRingBuffer.advanceReadPos();
                m_sampleRingBuffer.advanceWritePos();
            }
        }
        

        if (m_sampleRingBuffer.isReadable()) {
            m_demodPos = m_sampleRingBuffer.readPos();
            // extract phase shifted bits using manchester encoding
            for (size_t i = 0; i < Sampler::SampleBufferSize; i += Sampler::NumStreams) {
                for (size_t j = 0; j < Sampler::NumStreams; j++) {
                    // Think of having a sample stream of 2Mhz (so what we get from the planes)
                    // stream 0 << compare 0 and 1 
                    // stream 1 << compare 1 and 2
                    // 
                    // stream 0 << compare 2 and 3
                    // stream 1 << compare 3 and 4
                    // ....
                    // because the message might be shifted by one symbol
                    m_newBits[j] = m_demodPos[j] > m_demodPos[j + (Sampler::NumStreams >> 1)]; 
                    //m_sampleReadPos[i + j] > sampleReadPos[i + j + Sampler::SampleBufferOverlap];  
                }
                // and tell the demodulator to deal with the new bits
                demodCore.shiftInNewBits(m_newBits);
                // advance the readpos
                m_demodPos += Sampler::NumStreams;
            }
            m_sampleRingBuffer.advanceReadPos();
        }
    }
}
