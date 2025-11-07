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

#if !defined(COMPILER_INPUT_SAMPLE_RATE)
    #define COMPILER_INPUT_SAMPLE_RATE 0
#endif

constexpr int compilerInputSampleRate = COMPILER_INPUT_SAMPLE_RATE;

#if (COMPILER_INPUT_SAMPLE_RATE > 0)

        void run_main_passthrough() {
            constexpr SampleRate sampleRate = static_cast<SampleRate>(compilerInputSampleRate);
            typedef SamplerBase<sampleRate, sampleRate> sampler;
            // create an instance with a simple 1:1 sampler 
            SampleStream<sampler> sampleStream;
            // start reading the int16 iq data from airspy_rx
            sampleStream.read<IQ_AIRSPY_RX>(std::cin); 
        }

        void run_main_upsampling() {
            constexpr SampleRate inputRate = static_cast<SampleRate>(compilerInputSampleRate);
            constexpr SampleRate internalRate = static_cast<SampleRate>(compilerInputSampleRate * 2);
            typedef SamplerBase<inputRate, internalRate> sampler;
            // create an instance with a simple 1:1 sampler 
            SampleStream<sampler> sampleStream;
            // start reading the int16 iq data from airspy_rx
            sampleStream.read<IQ_AIRSPY_RX>(std::cin); 
        }
    
#else 
    void run_main_passthrough() {
        std::cerr << "[Stream1090] Input Sampling Rate: 2.4 Msps. Internal working speed: 8 Msps" << std::endl;
        // create the default rtl_sdr instance 
        SampleStream<Sampler_2_4_to_8_0_Mhz> sampleStream;
        // start reading thhe uint8 iq data from rtl_sdr
        sampleStream.read<IQ_RTL_SDR>(std::cin);
    }

    void run_main_upsampling() {
        run_main_passthrough();
    }
#endif

void printDeprecated() {
    std::cout << "Flags -a, -b, -c, -d, -f have been removed. Please use the corresponding executable instead." << std::endl;
    std::cout << "If for example your previous sample rate was 6Mhz and you were using the -b flag, then use stream1090_6M -u instead." << std::endl;
}

void printHelp() {
    if (compilerInputSampleRate == 0) {
        std::cout << "Stream 1090 build for standard rtl_sdr output @2.4MHz." << std::endl;
        std::cout << "Internal sample rate is 8MHz" << std::endl;
        std::cout << "There are no parameters available" << std::endl;
    } else {
        std::cout << "Stream 1090 build for int16 IQ output @" << compilerInputSampleRate / 1000000 << "MHz" << std::endl;
        std::cout << "The default internal sample rate is "<< compilerInputSampleRate / 1000000 << "MHz (passthrough)" << std::endl;
        std::cout << "-u : Enable upsampling to an internal sample rate of " << compilerInputSampleRate / 500000 << "MHz" << std::endl;
    }
}

int main(int argc, char** argv) {
    // Parse command line options.
    int upsampling = 0;
    int opt;
    while ((opt = getopt(argc, argv, "abcdfuh")) != -1) {
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
                upsampling = 1;
                break;
            case 'h':
                printHelp();
                return 0;
            break;
            default:
            break;
        }
    }
   
    if (upsampling > 0)
        run_main_upsampling();
    else
        run_main_passthrough();
    return 0;
}
