/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#include <iostream>
#include <fstream>
#include <unistd.h>
#include "SampleStream.hpp"

#define STREAM1090_VERSION "171125"

#if !defined(COMPILER_INPUT_SAMPLE_RATE)
    #define COMPILER_INPUT_SAMPLE_RATE 0
#endif

constexpr int compilerInputSampleRate = COMPILER_INPUT_SAMPLE_RATE;

template<typename Sampler>
void printSamplerConfig() {
    std::cerr << "[Stream1090] Input sampling speed: " << (double)Sampler::InputSampleRate / 1000000.0 << " MHz" << std::endl;
    std::cerr << "[Stream1090] Output sampling speed: " << Sampler::OutputSampleRate / 1000000 << " MHz" << std::endl;
    std::cerr << "[Stream1090] Input to output ratio: " << Sampler::RatioInput << ":" << Sampler::RatioOutput << std::endl;
    std::cerr << "[Stream1090] Number of streams: " << Sampler::NumStreams << std::endl;
    std::cerr << "[Stream1090] Size of input buffer: " << Sampler::InputBufferSize << " samples " << std::endl;
    std::cerr << "[Stream1090] Size of sample buffer: " << Sampler::SampleBufferSize << " samples " << std::endl;  
}



#if (COMPILER_INPUT_SAMPLE_RATE > 0)
    template<SampleRate outputRate>
    void runWithRate() {
        constexpr SampleRate inputRate = static_cast<SampleRate>(compilerInputSampleRate);
        typedef SamplerBase<inputRate, outputRate> sampler;
        // create an instance sampler  
        SampleStream<sampler> sampleStream;
        // start reading the int16 iq data from airspy_rx
        printSamplerConfig<sampler>();
        sampleStream.template read<IQ_AIRSPY_RX>(std::cin); 
    }
    
    #if (COMPILER_INPUT_SAMPLE_RATE == 6000000)
    void run_main(int internalSpeedMode) {
        switch (internalSpeedMode) {
            case 6:
                runWithRate<Rate_6_0_Mhz>();
            break;
            case 12: 
                runWithRate<Rate_12_0_Mhz>();
            break;
            case 16:
                runWithRate<Rate_16_0_Mhz>();
            break;
            case 24:
                runWithRate<Rate_24_0_Mhz>(); 
            break;
            default:
                std::cerr << "Unknown internal sample rate " << internalSpeedMode << std::endl;
            break;
            }
    }
    #endif
    #if (COMPILER_INPUT_SAMPLE_RATE == 10000000)
    void run_main(int internalSpeedMode) {
        switch (internalSpeedMode) {
            case 10:
                runWithRate<Rate_10_0_Mhz>();
            break;
            case 20: 
                runWithRate<Rate_20_0_Mhz>();
            break;
            case 24:
                runWithRate<Rate_24_0_Mhz>();
            break;
            default:
                std::cerr << "Unknown internal sample rate " << internalSpeedMode << std::endl;
            break;
            }
    }
    #endif
#else 
    void run_main() {
        // create the default rtl_sdr instance 
        SampleStream<Sampler_2_4_to_8_0_Mhz> sampleStream;
        // start reading thhe uint8 iq data from rtl_sdr
        printSamplerConfig<Sampler_2_4_to_8_0_Mhz>();
        sampleStream.read<IQ_RTL_SDR>(std::cin);
    }
#endif

void printDeprecated() {
    std::cout << "Flags -a, -b, -c, -d, -f have been removed. Please use the corresponding executable instead." << std::endl;
    std::cout << "If for example your previous sample rate was 6Mhz and you were using the -b flag, then use stream1090_6M -u instead." << std::endl;
}

void printHelp_0() {
    std::cout << "Stream 1090 build " << STREAM1090_VERSION << " for standard rtl_sdr output @2.4MHz." << std::endl;
    std::cout << "Internal sample rate is 8MHz" << std::endl;
    std::cout << "There are no parameters available" << std::endl;
}

void printHelp_6() {
    std::cout << "Stream 1090 build " << STREAM1090_VERSION << " for int16 IQ output @" << compilerInputSampleRate / 1000000 << "MHz" << std::endl;
    std::cout << "The default internal sample rate is "<< compilerInputSampleRate / 1000000 << "MHz (passthrough)" << std::endl;
    std::cout << "-u <6,12,16,24>: Sets the internal sample rate to 6 MHz, 12 MHz, 16 MHz, or 24 MHz" << std::endl;
}

void printHelp_10() {
    std::cout << "Stream 1090 build " << STREAM1090_VERSION << " for int16 IQ output @" << compilerInputSampleRate / 1000000 << "MHz" << std::endl;
    std::cout << "The default internal sample rate is "<< compilerInputSampleRate / 1000000 << "MHz (passthrough)" << std::endl;
    std::cout << "-u <10,20,24>: Sets the internal sample rate to 10 MHz, 20 MHz, or 24 MHz" << std::endl;
}


void printHelp() {
    if constexpr(compilerInputSampleRate == 0) {
        printHelp_0();
        return;
    }
    
    if constexpr(compilerInputSampleRate == Rate_6_0_Mhz) {
        printHelp_6();
        return;
    }

    if constexpr(compilerInputSampleRate == Rate_10_0_Mhz) {
        printHelp_10();
        return;
    }
}

int main(int argc, char** argv) {
    // Parse command line options.
    int upsampling = compilerInputSampleRate / 1000000;
    (void)upsampling;
    int opt;
    while ((opt = getopt(argc, argv, "abcdfu:h")) != -1) {
        switch (opt) {
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'f':
                printDeprecated();
                return -1;
                break;
            case 'u':
                upsampling = std::atoi(optarg);
                break;
            case 'h':
                printHelp();
                return 0;
            break;
            default:
            return 0;
            break;
        }
    }

#if (COMPILER_INPUT_SAMPLE_RATE > 0)
        run_main(upsampling);
#else
    run_main();
#endif
    return 0;
}
