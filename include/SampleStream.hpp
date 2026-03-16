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
#include "MathUtils.hpp"
#include "Sampler.hpp"
#include "MessageHandler.hpp"


// the main stream class. This class manages reading from the input stream
// and also manages the buffers
template<typename Sampler>
class SampleStream {

    struct DummyProvider {
        DummyProvider(const SampleStream<Sampler>& sampleStream) : m_sampleStream(sampleStream) {};

        uint8_t getRSSI(int streamIndex) const {
            return m_sampleStream.getRSSI(streamIndex);
        }

        const SampleStream<Sampler>& m_sampleStream;
    };
    public:
    //if personal use
#if defined(STREAM1090_OUTPUT_RAW) && STREAM1090_OUTPUT_RAW
    using MessageHandlerType = RawOutputMessageHandler<Sampler::NumStreams>;
#else
    #if defined(STREAM1090_RSSI) && STREAM1090_RSSI
        using MessageHandlerType = RssiStdOutMessageHandler<DummyProvider>;
    #else
        using MessageHandlerType = StdOutMessageHandler;
    #endif
#endif
    // for now we will keep one extra sample buffer as history
    static constexpr size_t NumSampleBuffers = 2;
    static constexpr size_t TotalSampleBufferLength = NumSampleBuffers * Sampler::SampleBufferSize + Sampler::SampleBufferOverlap;

    SampleStream() 
      : m_provider(*this),
#if defined(STREAM1090_RSSI) && STREAM1090_RSSI
        m_messageHandler(m_provider), 
#else
        m_messageHandler(),
#endif
        m_demod(m_messageHandler) 
    {
        // The resulting magnitude of the input samples which may also be used to directly read the magnitude of the samples
        // This buffer holds overlap many old values plus the new data  
        m_inputMagnitude = std::make_unique<float[]>(Sampler::InputBufferSize + Sampler::InputBufferOverlap);
    
        // the samples that we get from upsampling
        m_samples = std::make_unique<float[]>(TotalSampleBufferLength);
    }
   
    // the main method that streams from InputStream using inputReader
    template<typename InputReaderType>
    void read(InputReaderType& inputReader);

    uint8_t getRSSI(int) const {
        // we first have to compute the beginning of the message
        //streamIndex = (streamIndex + (Sampler::NumStreams >> 2)) % Sampler::NumStreams; 
        // the current m_readpos is 128 ahead in every stream with respect to the first bit of the message
        constexpr size_t bitDelay = 128-8;
        
        // how many samples are we ahead
        constexpr size_t samplesDelay = bitDelay * Sampler::NumStreams;

        // the number of samples that can be read before warping around (this excludes the overlap)
        constexpr size_t readBufferLength = NumSampleBuffers * Sampler::SampleBufferSize;

        // at which index are we reading right now?
        const size_t currReadPosIndex = m_readPos - m_samples.get(); 
        // we
        const size_t messageBegin = (currReadPosIndex + (readBufferLength - samplesDelay)) % readBufferLength;

        const float* readPos = m_samples.get() + messageBegin; 

        float res = 0.0;
        // this for now a very crude and shitty estimate
        for (size_t i = 0; i < Sampler::NumStreams; i++) {
            float f = std::max(readPos[i], readPos[i + (Sampler::NumStreams >> 1)]);
            res = std::max(res, f);
        }
        
        res = std::min(1.41f, res) / 1.41f;

        return uint8_t(res * 255.0);
    }

    private:

    DummyProvider m_provider;
    MessageHandlerType m_messageHandler;
    DemodCore<MessageHandlerType, Sampler::NumStreams> m_demod;
    uint32_t m_newBits[Sampler::NumStreams];
    
    std::unique_ptr<float[]> m_inputMagnitude;
	std::unique_ptr<float[]> m_samples;
    const float* m_readPos = nullptr;
    float* m_writePos = nullptr;
};


template<typename Sampler>
template<typename InputReaderType>
inline void SampleStream<Sampler>::read(InputReaderType& inputReader) {   
    // make sure the overlap parts at the beginning of the buffers are zeroed
    std::fill(m_inputMagnitude.get(), m_inputMagnitude.get() + Sampler::InputBufferOverlap, 0.0f);
    std::fill(m_samples.get(), m_samples.get() + Sampler::SampleBufferOverlap, 0.0f);

    // we have a buffer of sample buffers. The active one is referenced by this index.
    size_t currSampleBufferIndex = 0;

     // the main loop for reading the stream
    while (!inputReader.eof()) {
        // the read and write positions for the current sample buffer based its index.
        // we start reading at 0 + i * size           
        m_readPos = m_samples.get() + currSampleBufferIndex * Sampler::SampleBufferSize;
        // however, new values will be written NumStream / 2 later which is the overlap. 
        m_writePos = m_samples.get() + currSampleBufferIndex * Sampler::SampleBufferSize + Sampler::SampleBufferOverlap;
        
        // check if actually we need the sampler to resample, or if this is a 1:1 sampling
        if constexpr(Sampler::isPassthrough) {
            // tell the input reader to get us some data. Directly as magnitude. Since this is a passthrough sampler
            // we will directly read into the samples buffer. There is no need for using the sampler at all.
            // This works because the amount the input reader is getting us in this particular case is exactly the ChunkSize
            static_assert(Sampler::NumBlocks == Sampler::InputBufferSize);
            inputReader.readMagnitude(m_writePos);
        } else {
            // tell the input reader to get us some data. Directly as magnitude.
            inputReader.readMagnitude(m_inputMagnitude.get() + Sampler::InputBufferOverlap);
            // now ask the Sampler to resample the input magnitude to the output samples
            // similar to the input buffer, write after the overlap to keep some old values for the next iteration
            Sampler::sample(m_inputMagnitude.get(), m_writePos);
        }

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
            m_demod.shiftInNewBits(m_newBits);
            // advance the readpos
            m_readPos += Sampler::NumStreams;
        }
        
        // for the next round we move overlap many values from the end to the beginning of the input buffer
        std::memcpy(m_inputMagnitude.get(), m_inputMagnitude.get() + Sampler::InputBufferSize, Sampler::InputBufferOverlap * sizeof(float));

        // for the sample buffer we advance to the index to the next sample buffer.
        currSampleBufferIndex++;

        // if we reached the end of the buffer of sample buffers
        if (currSampleBufferIndex == NumSampleBuffers) {
            // reset the index
            currSampleBufferIndex = 0;
            // move overlap many samples from the end to the beginning. These are numstreams / 2 many.
            std::memcpy(m_samples.get(),  m_samples.get() + Sampler::SampleBufferSize * NumSampleBuffers, Sampler::SampleBufferOverlap * sizeof(float));
        }
    }
}
