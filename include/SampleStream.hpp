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
#include "InputReader.hpp"


// the main stream class. This class manages reading from the input stream
// and also manages the buffers
template<typename Sampler>
class SampleStream {
    public:
    SampleStream() {
        // The resulting magnitude of the input samples which may also be used to directly read the magnitude of the samples
        // This buffer holds overlap many old values plus the new data  
        m_inputMagnitude = std::make_unique<float[]>(Sampler::InputBufferSize + Sampler::InputBufferOverlap);
    
        // the samples that we get from upsampling
        m_samples = std::make_unique<float[]>(Sampler::SampleBufferSize + Sampler::SampleBufferOverlap);
    
        // make sure the overlap parts at the beginning of the buffers are zeroed
        std::fill(m_inputMagnitude.get(), m_inputMagnitude.get() + Sampler::InputBufferOverlap, 0.0f);
        std::fill(m_samples.get(), m_samples.get() + Sampler::SampleBufferOverlap, 0.0f);
    }
   
    // the main method 
    template<SampleStreamInputFormat inputFormat = IQ_RTL_SDR>
    void read(std::istream& inputStream);

    private:

    DemodCore<Sampler::NumStreams> m_demod;
    uint32_t m_newBits[Sampler::NumStreams];
    
    std::unique_ptr<float[]> m_inputMagnitude;
	std::unique_ptr<float[]> m_samples;
};


template<typename Sampler>
template<SampleStreamInputFormat inputFormat>
inline void SampleStream<Sampler>::read(std::istream& inputStream) {
    // create an instance for reading inputformat from inputStream
    // this will if needed also create its own buffer
    InputReader<Sampler, inputFormat> inputReader;
    
     // the main loop for reading the stream
    while (!inputStream.eof()) {
        // tell the input reader to get us some data. Directly as magnitude.
        inputReader.readMagnitude(inputStream, m_inputMagnitude.get() + Sampler::InputBufferOverlap);
                        
        // check if actually we need the sampler to resample, or if this is a 1:1 sampling
        if constexpr(Sampler::isPassthrough) {
            // this is a temporary solution. If the input sample rate == output sample rate, do not ask the sampler to deal with this
            std::memcpy(m_samples.get() + Sampler::SampleBufferOverlap, m_inputMagnitude.get(), Sampler::ChunkSize * sizeof(float));
        } else {
            // now ask the Sampler to resample the input magnitude to the output samples
            // similar to the input buffer, write after the overlap to keep some old values for the next iteration
            Sampler::sample(m_inputMagnitude.get(), m_samples.get() + Sampler::SampleBufferOverlap, Sampler::ChunkSize);
        }

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
                m_newBits[j] = m_samples[i + j] > m_samples[i + j + (Sampler::NumStreams >> 1)];    
            }
            // and tell the demodulator to deal with the new bits
            m_demod.shiftInNewBits(m_newBits);
        }
        
        // for the next round we move overlap many values from the end to the beginning
        std::memcpy(m_inputMagnitude.get(), m_inputMagnitude.get() + Sampler::InputBufferSize, Sampler::InputBufferOverlap * sizeof(float));
        std::memcpy(m_samples.get(),  m_samples.get() + Sampler::SampleBufferSize, Sampler::SampleBufferOverlap * sizeof(float));
    }
}
