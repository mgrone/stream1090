/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <thread>

#include "BlockRingAsync.hpp"
#include "DemodCore.hpp"
#include "Sampler.hpp"
#include "MessageHandler.hpp"

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
    BlockRingAsync<float, Sampler::InputBufferSize, NumInputBuffers,
                   Sampler::InputBufferOverlap, 1> m_inputRingBuffer;
    // and one for the upsampled magnitudes
    BlockRingAsync<float, Sampler::SampleBufferSize, NumSampleBuffers,
                   Sampler::SampleBufferOverlap, 1> m_sampleRingBuffer;
    // not nice. Will change
    const float* m_demodPos = nullptr;
};


template<typename Sampler>
template<typename InputReaderType, MessageHandler Handler>
inline void SampleStream<Sampler>::read(InputReaderType& inputReader, Handler& messageHandler) {  
    DemodCore<Sampler::NumStreams, Handler> demodCore(messageHandler);

    std::jthread inputThread;
    std::jthread samplerThread;

    if constexpr (Sampler::isPassthrough) {
        static_assert(Sampler::NumBlocks == Sampler::InputBufferSize);
        inputThread = std::jthread([&] {
            while (!inputReader.eof()) {
                float* output = m_sampleRingBuffer.waitWritePos();
                inputReader.readMagnitude(output);
                m_sampleRingBuffer.advanceWritePos();
            }
            m_sampleRingBuffer.finished();
        });
    } else {
        inputThread = std::jthread([&] {
            while (!inputReader.eof()) {
                float* output = m_inputRingBuffer.waitWritePos();
                inputReader.readMagnitude(output);
                m_inputRingBuffer.advanceWritePos();
            }
            m_inputRingBuffer.finished();
        });

        samplerThread = std::jthread([&] {
            while (const float* input = m_inputRingBuffer.waitReadPos()) {
                float* output = m_sampleRingBuffer.waitWritePos();
                Sampler::sample(input, output);
                m_inputRingBuffer.advanceReadPos();
                m_sampleRingBuffer.advanceWritePos();
            }
            m_sampleRingBuffer.finished();
        });
    }
        
    // main thread
    while ((m_demodPos = m_sampleRingBuffer.waitReadPos())) {
        for (size_t i = 0; i < Sampler::SampleBufferSize; i += Sampler::NumStreams) {
            for (size_t j = 0; j < Sampler::NumStreams; j++) {
                m_newBits[j] = m_demodPos[j] > m_demodPos[j + (Sampler::NumStreams >> 1)];
            }
            demodCore.shiftInNewBits(m_newBits);
            m_demodPos += Sampler::NumStreams;
        }

        m_sampleRingBuffer.advanceReadPos();
    }
}
