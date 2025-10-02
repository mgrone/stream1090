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
#include <cstring>

#include "MathUtils.hpp"
#include "Sampler.hpp"


// The input format for the sample stream
enum SampleStreamInputFormat {
    IQ_RTL_SDR, // the default output of from rtl_sdr
    IQ_AIRSPY_RX, // the default output of airspy_rx 
    MAG_FLOAT32, // for custom input when you want to read the magnitude as float directly
};


// Base template for the different input reader
template<typename Sampler, SampleStreamInputFormat InputFormat>
class InputReader {
    public:
    // default constructor
    InputReader();
    void readMagnitude(std::istream& inputStream, float* out);
};


// Partial specialization for rtl_sdr input
template<typename Sampler>
class InputReader<Sampler, IQ_RTL_SDR> {
public:
    // default constructor that will also allocate the buffer
    InputReader() {
        // we will need a buffer that holds Sampler::InputBufferSize many IQ pairs in the form of uint8 x 2 = uint16
        m_inputBuffer = std::make_unique<uint16_t[]>(Sampler::InputBufferSize);
    }

    void readMagnitude(std::istream& inputStream, float* out) {
        // Sampler::InputBufferSize many iq pairs from the input stream
        inputStream.read(reinterpret_cast<char*>(m_inputBuffer.get()), (Sampler::InputBufferSize) * sizeof(uint16_t));

        // for all of them
        for (size_t i = 0; i < Sampler::InputBufferSize; i++) {
            // compute the magnitude in float
            out[i] =  MathUtils::sqrt_iq(m_inputBuffer[i]);
        }
    }
private:
    std::unique_ptr<uint16_t[]> m_inputBuffer;
};


// Partial specialization for airspy int16 IQ input
template<typename Sampler>
class InputReader<Sampler, IQ_AIRSPY_RX> {
    public:
    // default constructor
    InputReader() {
        // we will need a buffer that holds Sampler::InputBufferSize many IQ pairs.
        // Each pair consists of two 16-bit signed ints
        m_inputBuffer = std::make_unique<int16_t[]>(Sampler::InputBufferSize << 1);
    }

    void readMagnitude(std::istream& inputStream, float* out) {
        // Sampler::InputBufferSize * 2 many 16 bit ints from the input stream
        inputStream.read(reinterpret_cast<char*>(m_inputBuffer.get()), (Sampler::InputBufferSize) * sizeof(int16_t) * 2);

        // call the helper function to compute the magnitude
        computeMagnitude(m_inputBuffer.get(), out, Sampler::InputBufferSize);
    }
    
    private:

    void computeMagnitude(int16_t* iq_single, float* out, size_t num) {
        for (size_t i = 0; i < num; i++) {
            // convert the two 16-bit integers to float.
            // however, airspy does not use the full 16 bits.
            // only 12 bits for each I and Q are used.
            const float f_i = (float)(*iq_single++) / 2048.0f;
			const float f_q = (float)(*iq_single++) / 2048.0f;
			const float sq = f_i * f_i + f_q * f_q;
            out[i] = sqrtf(sq); 
        }
    }

    // input buffer holding Sampler::InputBufferSize * 2 many ints
    std::unique_ptr<int16_t[]> m_inputBuffer;
};


// Partial specialization for reading directly float32 magnitude input
template<typename Sampler>
class InputReader<Sampler, MAG_FLOAT32> {
    public:
    // default constructor
    InputReader() {
        // we will not need any buffer for this
    }

    void readMagnitude(std::istream& inputStream, float* out) {
        // read directly Sampler::InputBufferSize many floats from the input stream
        inputStream.read(reinterpret_cast<char*>(out), (Sampler::InputBufferSize) * sizeof(float));
    }
};