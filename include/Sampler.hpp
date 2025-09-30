/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once
#include <numeric>

enum SampleRate {
    Rate_1_0_Mhz = 1000000,
    Rate_2_0_Mhz = 2000000,
    Rate_2_4_Mhz = 2400000,
    Rate_3_0_Mhz = 3000000,
    Rate_3_2_Mhz = 3200000,
    Rate_4_0_Mhz = 4000000,
    Rate_6_0_Mhz = 6000000,
    Rate_8_0_Mhz = 8000000
};

template<SampleRate _InputSampleRate, 
         SampleRate _OutputSampleRate, 
         size_t _InputBufferOverlap = 1>
class SamplerBase;

typedef SamplerBase<Rate_2_4_Mhz, Rate_6_0_Mhz> Sampler_2_4_to_6_0_Mhz;
typedef SamplerBase<Rate_3_0_Mhz, Rate_6_0_Mhz> Sampler_3_0_to_6_0_Mhz;
typedef SamplerBase<Rate_2_4_Mhz, Rate_4_0_Mhz> Sampler_2_4_to_4_0_Mhz;

// This class serves as descriptor for various values required for managing buffers and iterating over them
// All values are derived from the sample rates and optional the buffer overlap in case you want to write 
// a custom sampler that requires more overlap.
// WARNING: Do not fiddle around in here. You may adjust NumBlocksPerChunk. All other values are derived and
// changing them will most likely cause a mess. 
template<SampleRate _InputSampleRate, 
         SampleRate _OutputSampleRate, 
         size_t _InputBufferOverlap>
class SamplerBase {
    public:
    // The sample rate at which the data comes in from the SDR
    static constexpr SampleRate InputSampleRate  = _InputSampleRate;

    // The rate to which we upsample and then split in streams.
    // The ouput sample rate has to be a multiple of 2Mhz.
    // That's what we are getting from the planes
    static constexpr SampleRate OutputSampleRate = _OutputSampleRate;

    // no shenanigans allowed here. It is about upsampling
    static_assert(OutputSampleRate > InputSampleRate);

    // The output sample rate has to be a multiple of 2Mhz.
    static_assert(OutputSampleRate % Rate_2_0_Mhz == 0);

    // Each bit stream runs at 1Mhz, so the total number is
    static constexpr size_t NumStreams = OutputSampleRate / Rate_1_0_Mhz;

    // The ratio between the number of input samples and output samples.
    // In general this is InputSampleRate / gcd(InputSampleRate, OutputSampleRate)
    // OutputSampleRate / gcd(InputSampleRate, OutputSampleRate) (gcd = greatest common divisor)
    static constexpr size_t RatioInput  = InputSampleRate  / std::gcd((int)InputSampleRate, (int)OutputSampleRate);
    static constexpr size_t RatioOutput = OutputSampleRate / std::gcd((int)InputSampleRate, (int)OutputSampleRate);
    static_assert(InputSampleRate * RatioOutput == OutputSampleRate * RatioInput);

    // We are doing Manchester encoding, we therefore need to consider
    // some of the last samples from the previous run in the next pass.
    // This is exactly half the stream count many samples
    // (Note that the number of streams is always even by the assertion above)
    static constexpr size_t SampleBlockSize = NumStreams / 2; 
    
    static constexpr size_t NumBlocksPerChunk = 256;
    static constexpr size_t ChunkSize = NumBlocksPerChunk * SampleBlockSize;

    // The number of elements in the input and sample buffer which are considered fresh
    // This is not the total size of each buffer due to some overlap
    static constexpr size_t InputBufferSize  = RatioInput * ChunkSize; 
    static constexpr size_t SampleBufferSize = RatioOutput * ChunkSize; 
    
    static constexpr size_t InputBufferOverlap  = _InputBufferOverlap;
    static constexpr size_t SampleBufferOverlap = SampleBlockSize;

    // the main sampling function that has to be implemented
    static constexpr void sample(float* in, float* out, size_t numBlocks);    
};

// 2.4 Mhz to 4.0 Mhz (4 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_2_4_Mhz, Rate_4_0_Mhz>::sample(float* in, float* out, size_t numBlocks) {
    for (size_t i = 0; i < numBlocks; i++) {
            //  |00000|11111|22222|33333|
            //  +-----------------------+
            //  |..000|00022|22224|44444|
            //  |.....|11111|13333|33...|
            out[0] = (3.0f * in[0] + 3.0f * in[1]) * (1.0f / 6.0f);   
            out[1] = (5.0f * in[1] + 1.0f * in[2]) * (1.0f / 6.0f);
            out[2] = (2.0f * in[1] + 4.0f * in[2]) * (1.0f / 6.0f);
            out[3] = (4.0f * in[2] + 2.0f * in[3]) * (1.0f / 6.0f);
            out[4] = (1.0f * in[2] + 5.0f * in[3]) * (1.0f / 6.0f);
            in += 3;
            out += 5;
    }   
}

// 2.4 Mhz to 6.0 Mhz (6 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_2_4_Mhz, Rate_6_0_Mhz>::sample(float* in, float* out, size_t numBlocks) {
    for (size_t i = 0; i < numBlocks; i++) {
            //  |00000|11111|22222|
            //  +-----------------+
            //  |00000|03333|33...|
            //  |..111|11144|4444.|
            //  |....2|22222|.....|
            out[0] = (5.0f * in[0] + 1.0f * in[1]) * (1.0f / 6.0f);   
            out[1] = (3.0f * in[0] + 3.0f * in[1]) * (1.0f / 6.0f);
            out[2] = (1.0f * in[0] + 5.0f * in[1]) * (1.0f / 6.0f);
            out[3] = (4.0f * in[1] + 2.0f * in[2]) * (1.0f / 6.0f);
            out[4] = (2.0f * in[1] + 4.0f * in[2]) * (1.0f / 6.0f);
            in += 2;
            out += 5;
        }
}

// 3.0 Mhz to 6.0 Mhz (6 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_3_0_Mhz, Rate_6_0_Mhz>::sample(float* in, float* out, size_t numBlocks) {
    for (size_t i = 0; i < numBlocks; i++) {
            //  |00|11|
            //  +-----------------+
            //  |00|..|
            //  |.1|1.|
            //  |.....|
            out[0] = in[0];   
            out[1] = (in[0] + in[1]) / 2.0f;
            in += 1;
            out += 2;
        }
}


