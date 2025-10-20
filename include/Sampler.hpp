/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once
#include <numeric>

enum SampleRate {
    Rate_1_0_Mhz  =  1000000,
    Rate_2_0_Mhz  =  2000000,
    Rate_2_4_Mhz  =  2400000,
    Rate_3_0_Mhz  =  3000000,
    Rate_3_2_Mhz  =  3200000,
    Rate_4_0_Mhz  =  4000000,
    Rate_6_0_Mhz  =  6000000,
    Rate_8_0_Mhz  =  8000000,
    Rate_10_0_Mhz = 10000000,
    Rate_12_0_Mhz = 12000000,
    Rate_14_0_Mhz = 14000000,
    Rate_16_0_Mhz = 16000000,
    Rate_18_0_Mhz = 18000000,
    Rate_20_0_Mhz = 20000000
};

template<SampleRate _InputSampleRate, 
         SampleRate _OutputSampleRate, 
         size_t _InputBufferOverlap = 1>
class SamplerBase;

// one-to-one samplers 
typedef SamplerBase<Rate_2_0_Mhz,  Rate_2_0_Mhz>   Sampler_2_0_to_2_0_Mhz;
typedef SamplerBase<Rate_4_0_Mhz,  Rate_4_0_Mhz>   Sampler_4_0_to_4_0_Mhz;
typedef SamplerBase<Rate_6_0_Mhz,  Rate_6_0_Mhz>   Sampler_6_0_to_6_0_Mhz;
typedef SamplerBase<Rate_8_0_Mhz,  Rate_8_0_Mhz>   Sampler_8_0_to_8_0_Mhz;
typedef SamplerBase<Rate_10_0_Mhz, Rate_10_0_Mhz> Sampler_10_0_to_10_0_Mhz;
typedef SamplerBase<Rate_12_0_Mhz, Rate_12_0_Mhz> Sampler_12_0_to_12_0_Mhz;
typedef SamplerBase<Rate_16_0_Mhz, Rate_16_0_Mhz> Sampler_16_0_to_16_0_Mhz;
typedef SamplerBase<Rate_18_0_Mhz, Rate_18_0_Mhz> Sampler_18_0_to_18_0_Mhz;
typedef SamplerBase<Rate_20_0_Mhz, Rate_20_0_Mhz> Sampler_20_0_to_20_0_Mhz;

// 2.4 Mhz upsamplers 
typedef SamplerBase<Rate_2_4_Mhz, Rate_4_0_Mhz> Sampler_2_4_to_4_0_Mhz;
typedef SamplerBase<Rate_2_4_Mhz, Rate_6_0_Mhz> Sampler_2_4_to_6_0_Mhz;
typedef SamplerBase<Rate_2_4_Mhz, Rate_8_0_Mhz> Sampler_2_4_to_8_0_Mhz;

// 6 Mhz upsamplers
typedef SamplerBase<Rate_6_0_Mhz, Rate_12_0_Mhz> Sampler_6_0_to_12_0_Mhz;

// 10 Mhz upsampler
typedef SamplerBase<Rate_10_0_Mhz, Rate_20_0_Mhz> Sampler_10_0_to_20_0_Mhz;

// test sampler
typedef SamplerBase<Rate_3_0_Mhz, Rate_6_0_Mhz> Sampler_3_0_to_6_0_Mhz;


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
    static_assert(OutputSampleRate >= InputSampleRate);

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
    static_assert(ChunkSize % 2 == 0);
    // The number of elements in the input and sample buffer which are considered fresh
    // This is not the total size of each buffer due to some overlap
    // TODO ENSURE THAT SAMPLE BUFFER SIZE / SampleBlockSize is even
    static constexpr size_t InputBufferSize  = RatioInput * ChunkSize; 
    static constexpr size_t SampleBufferSize = RatioOutput * ChunkSize; 
    
    static constexpr size_t InputBufferOverlap  = _InputBufferOverlap;
    static constexpr size_t SampleBufferOverlap = SampleBlockSize;

    // if the input equals the output sample rate
    static constexpr bool isPassthrough = (InputSampleRate == OutputSampleRate);

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

// 2.4 Mhz to 8.0 Mhz (8 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_2_4_Mhz, Rate_8_0_Mhz>::sample(float* in, float* out, size_t numBlocks) {
    for (size_t i = 0; i < numBlocks; i++) {
        //  |0000000000|1111111111|2222222222|3333333333|
        //  +-------------------------------------------+
        //  |.000000000|0004444444|4444488888|8888888...|
        //  |....111111|1111115555|5555555599|9999999999|
        //  |.......222|2222222226|6666666666|6.........|
        //  |..........|3333333333|3377777777|7777......|
        out[0] = ( 9.0f * in[0] +  3.0f * in[1]) * (1.0f / 12.0f);   
        out[1] = ( 6.0f * in[0] +  6.0f * in[1]) * (1.0f / 12.0f);
        out[2] = ( 3.0f * in[0] +  9.0f * in[1]) * (1.0f / 12.0f);
        out[3] = (10.0f * in[1] +  2.0f * in[2]) * (1.0f / 12.0f);
        out[4] = ( 7.0f * in[1] +  5.0f * in[2]) * (1.0f / 12.0f);
        out[5] = ( 4.0f * in[1] +  8.0f * in[2]) * (1.0f / 12.0f);
        out[6] = ( 1.0f * in[1] + 10.0f * in[2] + 1.0f * in[3]) * (1.0f / 12.0f);
        out[7] = ( 8.0f * in[2] +  4.0f * in[3]) * (1.0f / 12.0f);
        out[8] = ( 5.0f * in[2] +  7.0f * in[3]) * (1.0f / 12.0f);
        out[9] = ( 2.0f * in[2] + 10.0f * in[3]) * (1.0f / 12.0f);
        in += 3;
        out += 10;
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

// 6.0 Mhz to 12.0 Mhz (12 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_6_0_Mhz, Rate_12_0_Mhz>::sample(float* in, float* out, size_t numBlocks) {
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

// 10.0 Mhz to 20.0 Mhz (20 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_10_0_Mhz, Rate_20_0_Mhz>::sample(float* in, float* out, size_t numBlocks) {
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