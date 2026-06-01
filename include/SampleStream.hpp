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

#include "DemodCore.hpp"
#include "Sampler.hpp"
#include "MessageHandler.hpp"

#pragma once
#include <memory>
#include <cstring>
#include <algorithm>

// will get is hpp anyways
template<typename T, size_t BlockSize, size_t NumBlocks, size_t Delay = 0>
class BlockRing {
public:
    static constexpr size_t TotalSize = BlockSize * NumBlocks + Delay;

    BlockRing(const T& initValue)
        : m_data(std::make_unique<T[]>(TotalSize)),
          m_readPos(0),
          m_writePos(Delay),
          m_fullBlocks(0)
    {
        std::fill(m_data.get(), m_data.get() + TotalSize, initValue);
    }

    // Pointer where the writer writes the next block
    T* writePos() {
        return m_data.get() + m_writePos;
    }

    // Commit one written block and advance the writer
    void advanceWritePos() {
        m_writePos += BlockSize;

        // Wrap condition: writing beyond the buffer end
        if (m_writePos + BlockSize > TotalSize) {

            // Copy last Delay samples to the front
            if constexpr (Delay > 0) {
                std::memcpy(
                    m_data.get(),                          // destination
                    m_data.get() + (TotalSize - Delay),    // source
                    Delay * sizeof(T)
                );
            }

            // Reset writer to the Delay offset
            m_writePos = Delay;
        }

        m_fullBlocks++;
    }

    // Pointer where the reader reads the next block
    const T* readPos() const {
        return m_data.get() + m_readPos;
    }

    // Commit one consumed block and advance the reader
    void advanceReadPos() {
        m_readPos += BlockSize;

        // Reader wraps after NumBlocks blocks
        if (m_readPos >= BlockSize * NumBlocks) {
            m_readPos = 0;
        }

        m_fullBlocks--;
    }

    // Reader can check if a block is available
    bool isReadable() const {
        return m_fullBlocks > 0;
    }

    // Writer can check if space is available
    bool isWritable() const {
        return m_fullBlocks < NumBlocks;
    }

    const T& lookBack(size_t k, size_t offsetInBlock = 0) const {
        // Convert read pointer + offset to absolute ring index
        size_t absoluteIndex = m_readPos + offsetInBlock;
        if (absoluteIndex >= TotalSize)
            absoluteIndex -= TotalSize;

        // Now go back k samples
        size_t lookBackIndex = absoluteIndex + TotalSize - k;
        if (lookBackIndex >= TotalSize)
            lookBackIndex -= TotalSize;

        return m_data[lookBackIndex];
    }

private:
    std::unique_ptr<T[]> m_data;

    size_t m_readPos;      // next block to read
    size_t m_writePos;     // next block to write
    size_t m_fullBlocks;   // number of complete blocks available
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

    SampleStream() : m_inputRingBuffer(0.0f), m_sampleRingBuffer(0.0f) { }
   
    // the main method that streams from InputStream using inputReader
    template<typename InputReaderType, MessageHandler Handler>
    void read(InputReaderType& inputReader, Handler& messageHandler);

    uint8_t getRSSI(int) const {
        // we are 128 bits behind and are looking for the preamble pulse
        constexpr size_t bitDelay     = 128 - 8;
        // how much is that in samples?
        constexpr size_t samplesDelay = bitDelay * Sampler::NumStreams;
        // how far is the demodulator in the block?
        const auto offsetInBlock = m_readPos - m_sampleRingBuffer.readPos();
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
    BlockRing<float, Sampler::InputBufferSize,  NumInputBuffers,  Sampler::InputBufferOverlap>  m_inputRingBuffer;
    // and one for the upsampled magnitudes
    BlockRing<float, Sampler::SampleBufferSize, NumSampleBuffers, Sampler::SampleBufferOverlap> m_sampleRingBuffer;
    // not nice. Will change
    const float* m_readPos = nullptr;
};


template<typename Sampler>
template<typename InputReaderType, MessageHandler Handler>
inline void SampleStream<Sampler>::read(InputReaderType& inputReader, Handler& messageHandler) {  
    // the core logic for message recognition
    DemodCore<Sampler::NumStreams, Handler> demodCore(messageHandler);

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
            //inputReader.readMagnitude(m_inputMagnitude.get() + Sampler::InputBufferOverlap);
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
            m_readPos = m_sampleRingBuffer.readPos();
            // extract phase shifted bits using manchester encoding
            for (size_t i = 0; i < Sampler::SampleBufferSize; i += Sampler::NumStreams) {
            // numIterations++;
                for (size_t j = 0; j < Sampler::NumStreams; j++) {
                    // Think of having a sample stream of 2Mhz (so what we get from the planes)
                    // stream 0 << compare 0 and 1 
                    // stream 1 << compare 1 and 2
                    // 
                    // stream 0 << compare 2 and 3
                    // stream 1 << compare 3 and 4
                    // ....
                    // because the message might be shifted by one symbol
                    //m_newBits[j] = sampleReadPos[i + j] > sampleReadPos[i + j + Sampler::SampleBufferOverlap];  
                    m_newBits[j] = m_readPos[j] > m_readPos[j + (Sampler::NumStreams >> 1)]; //m_sampleReadPos[i + j] > sampleReadPos[i + j + Sampler::SampleBufferOverlap];  
                }
                // and tell the demodulator to deal with the new bits
                demodCore.shiftInNewBits(m_newBits);
                // advance the readpos
                m_readPos += Sampler::NumStreams;
            }
            m_sampleRingBuffer.advanceReadPos();
        }
    }
}
