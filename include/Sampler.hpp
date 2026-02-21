/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once
#include <numeric>
#include <cstddef>
#include "SamplerFunc.hpp"

enum SampleRate {
    Rate_1_0_Mhz  =  1000000,
    Rate_2_0_Mhz  =  2000000,
    Rate_2_4_Mhz  =  2400000,
    Rate_2_56_Mhz =  2560000,
    Rate_3_0_Mhz  =  3000000,
    Rate_3_2_Mhz  =  3200000,
    Rate_4_0_Mhz  =  4000000,
    Rate_6_0_Mhz  =  6000000,
    Rate_8_0_Mhz  =  8000000,
    Rate_10_0_Mhz = 10000000,
    Rate_12_0_Mhz = 12000000,
    Rate_16_0_Mhz = 16000000,
    Rate_20_0_Mhz = 20000000,
    Rate_24_0_Mhz = 24000000,
    Rate_40_0_Mhz = 40000000,
    Rate_48_0_Mhz = 48000000
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
typedef SamplerBase<Rate_20_0_Mhz, Rate_20_0_Mhz> Sampler_20_0_to_20_0_Mhz;
typedef SamplerBase<Rate_24_0_Mhz, Rate_24_0_Mhz> Sampler_24_0_to_24_0_Mhz;

// 2.0 Mhz upsamplers
typedef SamplerBase<Rate_2_0_Mhz, Rate_4_0_Mhz> Sampler_2_0_to_4_0_Mhz;
typedef SamplerBase<Rate_2_0_Mhz, Rate_8_0_Mhz> Sampler_2_0_to_8_0_Mhz;

// 2.4 Mhz upsamplers 
typedef SamplerBase<Rate_2_4_Mhz, Rate_4_0_Mhz> Sampler_2_4_to_4_0_Mhz;
typedef SamplerBase<Rate_2_4_Mhz, Rate_6_0_Mhz> Sampler_2_4_to_6_0_Mhz;
typedef SamplerBase<Rate_2_4_Mhz, Rate_8_0_Mhz> Sampler_2_4_to_8_0_Mhz;
typedef SamplerBase<Rate_2_4_Mhz, Rate_12_0_Mhz> Sampler_2_4_to_12_0_Mhz;

// 2.56 MHz upsamplers
typedef SamplerBase<Rate_2_56_Mhz, Rate_8_0_Mhz> Sampler_2_56_to_8_0_Mhz;
typedef SamplerBase<Rate_2_56_Mhz, Rate_12_0_Mhz> Sampler_2_56_to_12_0_Mhz;

// 6 Mhz upsamplers
typedef SamplerBase<Rate_6_0_Mhz, Rate_12_0_Mhz> Sampler_6_0_to_12_0_Mhz;
typedef SamplerBase<Rate_6_0_Mhz, Rate_16_0_Mhz> Sampler_6_0_to_16_0_Mhz;
typedef SamplerBase<Rate_6_0_Mhz, Rate_24_0_Mhz> Sampler_6_0_to_24_0_Mhz;

// 10 Mhz upsampler
typedef SamplerBase<Rate_10_0_Mhz, Rate_20_0_Mhz> Sampler_10_0_to_20_0_Mhz;
typedef SamplerBase<Rate_10_0_Mhz, Rate_24_0_Mhz> Sampler_10_0_to_24_0_Mhz;

// 12 Mhz upsampler
typedef SamplerBase<Rate_12_0_Mhz, Rate_24_0_Mhz> Sampler_12_0_to_24_0_Mhz;

// test sampler
typedef SamplerBase<Rate_3_0_Mhz, Rate_6_0_Mhz> Sampler_3_0_to_6_0_Mhz;
typedef SamplerBase<Rate_20_0_Mhz, Rate_24_0_Mhz> Sampler_20_0_to_24_0_Mhz;
typedef SamplerBase<Rate_20_0_Mhz, Rate_40_0_Mhz> Sampler_20_0_to_40_0_Mhz;

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
    
    
    // This is the desired input buffer size. This will be a lower bound.
    static constexpr size_t DesiredInputBufferSize = 8192;
    static constexpr size_t NumBlocks = (DesiredInputBufferSize / (RatioInput * SampleBlockSize * 2) + 1) * (SampleBlockSize * 2);
    
    
    /*static constexpr size_t NumBlocksPerChunk = 256;
    static constexpr size_t ChunkSize = NumBlocksPerChunk * SampleBlockSize; */
    
    static_assert(NumBlocks % 2 == 0);
    // The number of elements in the input and sample buffer which are considered fresh
    // This is not the total size of each buffer due to some overlap
    // TODO ENSURE THAT SAMPLE BUFFER SIZE / SampleBlockSize is even
    static constexpr size_t InputBufferSize  = RatioInput * NumBlocks; 
    static constexpr size_t SampleBufferSize = RatioOutput * NumBlocks; 
    
    static constexpr size_t InputBufferOverlap  = _InputBufferOverlap;
    static constexpr size_t SampleBufferOverlap = SampleBlockSize;

    // if the input equals the output sample rate
    static constexpr bool isPassthrough = (InputSampleRate == OutputSampleRate);

    // the main sampling function that has to be implemented
    static constexpr void sample(float* __restrict in, float* __restrict out) noexcept {
        SamplerFunc<RatioInput, RatioOutput, NumBlocks>::sample(in, out);
    };    
};


// 2.4 Mhz to 4.0 Mhz (4 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_2_4_Mhz, Rate_4_0_Mhz>::sample(float* __restrict in, float* __restrict out) noexcept {
    for (size_t i = 0; i < NumBlocks; i++) {
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
constexpr void SamplerBase<Rate_2_4_Mhz, Rate_6_0_Mhz>::sample(float* __restrict in, float* __restrict out) noexcept {
    for (size_t i = 0; i < NumBlocks; i++) {
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
constexpr void SamplerBase<Rate_2_4_Mhz, Rate_8_0_Mhz>::sample(float* __restrict in, float* __restrict out) noexcept {
    for (size_t i = 0; i < NumBlocks; i++) {
        //  |0000000000|1111111111|2222222222|3333333333|
        //  +-------------------------------------------+
        //  |...0000000|0....44444|444....888|88888.....|
        //  |......1111|1111....55|555555....|99999999..|
        //  |.........2|2222222...|.66666666.|..........|
        //  |..........|..33333333|....777777|77........|
        out[0] = ( 7.0f * in[0] +  1.0f * in[1]) * (1.0f / 8.0f);   
        out[1] = ( 4.0f * in[0] +  4.0f * in[1]) * (1.0f / 8.0f);
        out[2] = ( 1.0f * in[0] +  7.0f * in[1]) * (1.0f / 8.0f);
        out[3] = ( 8.0f * in[1] +  0.0f * in[2]) * (1.0f / 8.0f);
        out[4] = ( 5.0f * in[1] +  3.0f * in[2]) * (1.0f / 8.0f);
        out[5] = ( 2.0f * in[1] +  6.0f * in[2]) * (1.0f / 8.0f);
        out[6] = ( 8.0f * in[2] +  0.0f * in[3]) * (1.0f / 8.0f);
        out[7] = ( 6.0f * in[2] +  2.0f * in[3]) * (1.0f / 8.0f);
        out[8] = ( 3.0f * in[2] +  5.0f * in[3]) * (1.0f / 8.0f);
        out[9] = ( 0.0f * in[2] +  8.0f * in[3]) * (1.0f / 8.0f);
        in += 3;
        out += 10;
    } 
}

// 2.56 Mhz to 8.0 Mhz (8 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_2_56_Mhz, Rate_8_0_Mhz>::sample(float* __restrict in, float* __restrict out) noexcept {
    for (size_t i = 0; i < NumBlocks; i++) {
            for (int j = 0; j < 25; j++) {
                const auto offset = 8 * j;
                const auto k = offset / 25;
                const auto l = 24 - (offset % 25);
                const auto r = 24 - l;
                out[j] = ((float)l * in[k] + (float)r * in[k+1]) * (1.0f / 24.0f);
            }
            in += 8;//5;
            out += 25;//12;
        }
}

// 6.0 Mhz to 16.0 Mhz (16 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_6_0_Mhz, Rate_16_0_Mhz>::sample(float* __restrict in, float* __restrict out) noexcept {
   for (size_t i = 0; i < NumBlocks; i++) {
            //  |00000000|11111111|22222222|33333333|
            //  +-----------------------------------+
            //  |00000000|........|........|........|
            //  |...11111|111.....|........|........|
            //  |......22|222222..|........|........|
            //  |........|.3333333|3.......|........|
            //  |........|....4444|4444....|........|
            //  |........|.......5|5555555.|........|
            //  |........|........|..666666|66......|
            //  |........|........|.....777|77777...|
            //  |........|........|........|88888888|
            out[0] = ( 8.0f * in[0] +  0.0f * in[1]) * (1.0f / 8.0f);   
            out[1] = ( 5.0f * in[0] +  3.0f * in[1]) * (1.0f / 8.0f);
            out[2] = ( 2.0f * in[0] +  6.0f * in[1]) * (1.0f / 8.0f);
            out[3] = ( 7.0f * in[1] +  1.0f * in[2]) * (1.0f / 8.0f);
            out[4] = ( 4.0f * in[1] +  4.0f * in[2]) * (1.0f / 8.0f);
            out[5] = ( 1.0f * in[1] +  7.0f * in[2]) * (1.0f / 8.0f);
            out[6] = ( 6.0f * in[2] +  2.0f * in[3]) * (1.0f / 8.0f);
            out[7] = ( 3.0f * in[2] +  5.0f * in[3]) * (1.0f / 8.0f);
            in += 3;
            out += 8;
        }
} 

// 10.0 Mhz to 24.0 Mhz (24 streams) upsampling function
template<>
constexpr void SamplerBase<Rate_10_0_Mhz, Rate_24_0_Mhz>::sample(float* __restrict in, float* __restrict out) noexcept {
    for (size_t i = 0; i < NumBlocks; i++) {
            for (int j = 0; j < 12; j++) {
                const auto offset = 5 * j;
                const auto k = offset / 12;
                const auto l = 9 - (offset % 10);
                const auto r = 9 - l;
                out[j] = ((float)l * in[k] + (float)r * in[k+1]) * (1.0f / 9.0f);
            }
            in += 5;
            out += 12;
        }
}
