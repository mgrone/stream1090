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
    IQ_RTL_SDR, // the default output of from rtl_sdr
    IQ_AIRSPY_RX, // the default output of airspy_rx 
    MAG_FLOAT32, // for custom input when you want to read the magnitude as float directly
};

// the main stream class. This class manages reading from the input stream
// and also manages the buffers
template<typename Sampler>
class SampleStream {
    public:
   
    // the main method 
    template<SampleStreamInputFormat inputFormat = IQ_RTL_SDR>
    void read(std::istream& inputStream);

    private:
    
    static void computeMagnitude_RTL_SDR(uint16_t* iq_pair, float* out, size_t num) {
        for (size_t i = 0; i < num; i++) {
            /*const uint16_t I = iq_pair[i] >> 8;
            const uint16_t Q = iq_pair[i] & 0xff;
            const float f_i = ((float)I - 127.5f) / 127.5f;
			const float f_q = ((float)Q - 127.5f) / 127.5f;
			const float sq = f_i * f_i + f_q * f_q; */
            out[i] =  MathUtils::sqrt_iq(iq_pair[i]);
        }
    }

    static void computeMagnitude_AIRSPY_RX(int16_t* iq_single, float* out, size_t num) {
        for (size_t i = 0; i < num; i++) {
            // convert the two 16-bit integers to float.
            // however, airspy does not use the full 16 bit.
            // only 12 bits for each I and Q are used.
            const float f_i = (float)(*iq_single++) / 2048.0f;
			const float f_q = (float)(*iq_single++) / 2048.0f;
			const float sq = f_i * f_i + f_q * f_q;
            out[i] = sqrtf(sq); 
        }
    }
    private:

    DemodCore<Sampler::NumStreams> m_demod;
    uint32_t m_newBits[Sampler::NumStreams];
    
    std::unique_ptr<uint16_t[]> m_inputBuffer;
    std::unique_ptr<float[]> m_inputMagnitude;
	std::unique_ptr<float[]> m_samples;
};


template<typename Sampler>
template<SampleStreamInputFormat inputFormat>
inline void SampleStream<Sampler>::read(std::istream& inputStream) {
    // A buffer to read the iq pairs from the stream.
    // This buffer holds the fresh iq pairs.
    // We distinguish cases here on if we need this buffer or not.
    // If we read directly the magnitude, we don't allocate the buffer
    // distinguish cases based on the input
    if constexpr (inputFormat == IQ_RTL_SDR) {
        m_inputBuffer = std::make_unique<uint16_t[]>(Sampler::InputBufferSize);
    } else if constexpr (inputFormat == IQ_AIRSPY_RX) {
        m_inputBuffer = std::make_unique<uint16_t[]>(Sampler::InputBufferSize * 2);
    } else {
        m_inputBuffer = nullptr;
    }
    
    // The resulting magnitude of the input samples which may also be used to directly read the magnitude of the samples
    // This buffer holds overlap many old values plus the new data  
    m_inputMagnitude = std::make_unique<float[]>(Sampler::InputBufferSize + Sampler::InputBufferOverlap);
   
    // the samples that we get from upsampling
    m_samples = std::make_unique<float[]>(Sampler::SampleBufferSize + Sampler::SampleBufferOverlap);
   
    // make sure the overlap parts at the beginning of the buffers are zeroed
    std::fill(m_inputMagnitude.get(), m_inputMagnitude.get() + Sampler::InputBufferOverlap, 0.0f);
    std::fill(m_samples.get(), m_samples.get() + Sampler::SampleBufferOverlap, 0.0f);

     // the main loop for reading the stream
    while (!inputStream.eof()) {

        // distinguish cases based on the input
        if constexpr (inputFormat == IQ_RTL_SDR) {
             // read a new chunk of iq pairs from the input 
            inputStream.read(reinterpret_cast<char*>(m_inputBuffer.get()), (Sampler::InputBufferSize) * sizeof(uint16_t));
            // compute the magnitude of the new iq data. 
            // Make sure to store them starting after the overlap 
            // which contains values from the previous round
            computeMagnitude_RTL_SDR(m_inputBuffer.get(), m_inputMagnitude.get() + Sampler::InputBufferOverlap, Sampler::InputBufferSize);
        } else if constexpr (inputFormat == IQ_AIRSPY_RX) {
            // read a new chunk of iq pairs from the input, however, an IQ pair consists of two 16bit ints
            inputStream.read(reinterpret_cast<char*>(m_inputBuffer.get()), (Sampler::InputBufferSize) * sizeof(uint16_t) * 2);
            // compute the magnitude with the corresponding function
            // Make sure to store them starting after the overlap 
            // which contains values from the previous round
            computeMagnitude_AIRSPY_RX(reinterpret_cast<int16_t*>(m_inputBuffer.get()), m_inputMagnitude.get() + Sampler::InputBufferOverlap, Sampler::InputBufferSize);
        } else if constexpr (inputFormat == MAG_FLOAT32) {
            // read a new chunk of floats from the input, but store them after the overlap
            inputStream.read(reinterpret_cast<char*>(m_inputMagnitude.get() + Sampler::InputBufferOverlap), (Sampler::InputBufferSize) * sizeof(float));
        } else {
            static_assert(false);
        } 
                
        // now ask the Sampler to resample the input magnitude to the output samples
        // similar to the input buffer, write after the overlap to keep some old values for the next iteration
        Sampler::sample(m_inputMagnitude.get(), m_samples.get() + Sampler::SampleBufferOverlap, Sampler::ChunkSize);

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
