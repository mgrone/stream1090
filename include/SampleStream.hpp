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

// The input format for the sample stream
enum SampleStreamInputFormat {
    IQ_uint8, // the default that comes from rtl_sdr
    MAG_float, // for custom input when you want to read the magnitude as float directly
};

// the main stream class. This class manages reading from the input stream
// and also manages the buffers
template<typename Sampler>
class SampleStream {
    public:
   
    template<SampleStreamInputFormat inputFormat = IQ_uint8>
    void read(std::istream& inputStream);


    private:
    
    static void computeMagnitude(uint16_t* iq, float* out, size_t num) {
        for (size_t i = 0; i < num; i++) {
            out[i] = MathUtils::sqrt_iq(iq[i]);
        }
    }

    uint32_t m_newBits[Sampler::NumStreams];
    DemodCore<Sampler::NumStreams> m_demod;
};

template<typename Sampler>
template<SampleStreamInputFormat inputFormat>
void SampleStream<Sampler>::read(std::istream& inputStream) {
    // A buffer to read the iq pairs from the stream.
    // This buffer holds the fresh iq pairs.
    // We distinguish cases here on if we need this buffer or not.
    // If we read directly the magnitude, we don't allocate the buffer
    const auto inputBuffer = (inputFormat == IQ_uint8) ? std::make_unique<uint16_t[]>(Sampler::InputBufferSize) : nullptr;
    
    // The resulting magnitude of the input samples which may also be used to directly read the magnitude of the samples
    // This buffer holds overlap many old values plus the new data  
    const auto inputMagnitude = std::make_unique<float[]>(Sampler::InputBufferSize + Sampler::InputBufferOverlap);

    // the samples that we get from upsampling
    const auto samples = std::make_unique<float[]>(Sampler::SampleBufferSize + Sampler::SampleBufferOverlap);

    // make sure the overlap parts at the beginning of the buffers are zeroed
    std::fill(inputMagnitude.get(), inputMagnitude.get() + Sampler::InputBufferOverlap, 0.0f);
    std::fill(samples.get(), samples.get() + Sampler::SampleBufferOverlap, 0.0f);

     // the main loop for reading the stream
    while (!inputStream.eof()) {

        // distinguish cases based on the input
        if (inputFormat == IQ_uint8) {
             // read a new chunk of iq pairs from the input 
            inputStream.read(reinterpret_cast<char*>(inputBuffer.get()), (Sampler::InputBufferSize) * sizeof(uint16_t));

            // compute the magnitude of the new iq data. 
            // Make sure to store them starting after the overlap 
            // which contains values from the previous round
            computeMagnitude(inputBuffer.get(), inputMagnitude.get() + Sampler::InputBufferOverlap, Sampler::InputBufferSize);
        } else {
             // read a new chunk of floats from the input, but store them after the overlap
            inputStream.read(reinterpret_cast<char*>(inputMagnitude.get() + Sampler::InputBufferOverlap), (Sampler::InputBufferSize) * sizeof(float));
        }
                
        // now ask the Sampler to resample the input magnitude to the output samples
        // similar to the input buffer, write after the overlap to keep some old values for the next iteration
        Sampler::sample(inputMagnitude.get(), samples.get() + Sampler::SampleBufferOverlap, Sampler::ChunkSize);

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
                m_newBits[j] = samples[i + j] > samples[i + j + (Sampler::NumStreams >> 1)];    
            }
            // and tell the demodulator to deal with the new bits
            m_demod.shiftInNewBits(m_newBits);
        }
        
        // for the next round we move overlap many values from the end to the beginning
        std::memcpy(inputMagnitude.get(), inputMagnitude.get() + Sampler::InputBufferSize, Sampler::InputBufferOverlap * sizeof(float));
        std::memcpy(samples.get(),  samples.get() + Sampler::SampleBufferSize, Sampler::SampleBufferOverlap * sizeof(float));
    }
}
