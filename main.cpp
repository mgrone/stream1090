/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <algorithm>
#include "SampleStream.hpp"

#define STREAM1090_VERSION "260120"


#if !defined(COMPILER_INPUT_SAMPLE_RATE)
    #define COMPILER_INPUT_SAMPLE_RATE 0
#endif

constexpr int compilerInputSampleRate = (COMPILER_INPUT_SAMPLE_RATE == 0) ? 2400000 : COMPILER_INPUT_SAMPLE_RATE;

template<typename Sampler>
void printSamplerConfig() {
    std::cerr << "[Stream1090] build " << STREAM1090_VERSION << std::endl;
    std::cerr << "[Stream1090] Input sampling speed: " << (double)Sampler::InputSampleRate / 1000000.0 << " MHz" << std::endl;
    std::cerr << "[Stream1090] Output sampling speed: " << Sampler::OutputSampleRate / 1000000 << " MHz" << std::endl;
    std::cerr << "[Stream1090] Input to output ratio: " << Sampler::RatioInput << ":" << Sampler::RatioOutput << std::endl;
    std::cerr << "[Stream1090] Number of streams: " << Sampler::NumStreams << std::endl;
    std::cerr << "[Stream1090] Size of input buffer: " << Sampler::InputBufferSize << " samples " << std::endl;
    std::cerr << "[Stream1090] Size of sample buffer: " << Sampler::SampleBufferSize << " samples " << std::endl;  
}

void printDeprecated() {
    std::cerr << "Flags -a, -b, -c, -d, -f have been removed. Please use the corresponding executable instead." << std::endl;
    std::cerr << "If for example your previous sample rate was 6Mhz and you were using the -b flag, then use stream1090_6M -u instead." << std::endl;
}

template<int inputRate>
void printHelp();

template<>
void printHelp<Rate_2_4_Mhz>() {
    std::cerr << "Stream 1090 build " << STREAM1090_VERSION << " for standard rtl_sdr output @2.4MHz." << std::endl;
    std::cerr << "Internal sample rate is 8MHz" << std::endl;
    std::cerr << "-m : Stream1090 expects the magnitude in float32 instead of IQ pairs." << std::endl;
}

template<>
void printHelp<Rate_6_0_Mhz>() {
    std::cerr << "Stream 1090 build " << STREAM1090_VERSION << " for airspy_rx output @" << compilerInputSampleRate / 1000000 << "MHz" << std::endl;
    std::cerr << "The default internal sample rate is "<< compilerInputSampleRate / 1000000 << "MHz (passthrough)" << std::endl;
    std::cerr << "-u <6,12,16,24>: Sets the internal sample rate to 6 MHz, 12 MHz, 16 MHz, or 24 MHz" << std::endl;
    std::cerr << "-r : Stream1090 expects raw airspy_rx output (airspy_rx... -t 5)." << std::endl; 
    std::cerr << "     Requires double the sample rate for airspy_rx (airspy_rx... -a "<< compilerInputSampleRate * 2 << ")" << std::endl;
    std::cerr << "-q : Similar to -r, but includes a proper low pass filtering for I and Q channels." << std::endl;
    std::cerr << "-m : Stream1090 expects the magnitude in float32 instead of IQ pairs." << std::endl;
}

template<>
void printHelp<Rate_10_0_Mhz>() {
    std::cerr << "Stream 1090 build " << STREAM1090_VERSION << " for airspy_rx output @" << compilerInputSampleRate / 1000000 << "MHz" << std::endl;
    std::cerr << "The default internal sample rate is "<< compilerInputSampleRate / 1000000 << "MHz (passthrough)" << std::endl;
    std::cerr << "-u <10,20,24>: Sets the internal sample rate to 10 MHz, 20 MHz, or 24 MHz" << std::endl;
    std::cerr << "-r : Stream1090 expects raw airspy_rx output (airspy_rx... -t 5)." << std::endl; 
    std::cerr << "     Requires double the sample rate for airspy_rx (airspy_rx... -a " << compilerInputSampleRate * 2 << ")" << std::endl;
    std::cerr << "-q : Similar to -r, but includes a proper low pass filtering for I and Q channels." << std::endl;
    std::cerr << "-m : Stream1090 expects the magnitude in float32 instead of IQ pairs." << std::endl;
}


template<int inputRate>
bool check_upsampling_parameter(int upsamplingParameter);

template<>
bool check_upsampling_parameter<Rate_2_4_Mhz>(int upsamplingParameter) {
    return (upsamplingParameter == 8);
}

template<>
bool check_upsampling_parameter<Rate_6_0_Mhz>(int upsamplingParameter) {
    const auto allowed = { 6, 12, 16, 24 };
    return (std::find(std::begin(allowed), std::end(allowed), upsamplingParameter) != std::end(allowed));
}

template<>
bool check_upsampling_parameter<Rate_10_0_Mhz>(int upsamplingParameter) {
    const auto allowed = { 10, 20, 24 };
    return (std::find(std::begin(allowed), std::end(allowed), upsamplingParameter) != std::end(allowed));
}


template<SampleRate inputRate, SampleRate outputRate, SampleStreamInputFormat inputFormat>
bool run_main_templ(const std::string& tapsFile) {
    typedef SamplerBase<inputRate, outputRate> sampler;
    // create an instance sampler  
    SampleStream<sampler> sampleStream;
    // start reading the int16 iq data from airspy_rx
#if defined(STATS_ENABLED) && STATS_ENABLED
    printSamplerConfig<sampler>();
#endif

    if constexpr (inputFormat == IQ_AIRSPY_RX_RAW_IQ_FILTER_FILE) {
        InputReader<sampler, IQ_AIRSPY_RX_RAW_IQ_FILTER_FILE> reader;

        if (!reader.loadTapsFromFile(tapsFile)) {       // you add this method
            std::cerr << "Failed to load taps from " << tapsFile << "\n";
            return false;
        }

        sampleStream.read(std::cin, reader);
        return true;
    } else {
        sampleStream.template read<inputFormat>(std::cin);
        return true;
    }
}

template<SampleStreamInputFormat inputFormat>
bool run_main(int upsampling, const std::string& tapsFile = "") {
    constexpr SampleRate inputRate = static_cast<SampleRate>(compilerInputSampleRate);
    if constexpr(inputRate == Rate_2_4_Mhz) {
        switch (upsampling) {
            case 8:
                return run_main_templ<inputRate, Rate_8_0_Mhz, inputFormat>(tapsFile);
                // debugging:
                //run_main_templ<Rate_12_0_Mhz, Rate_12_0_Mhz, inputFormat>();
            break;
            default:
                std::cerr << "Unknown internal sample rate " << upsampling << std::endl;
                return false;
            break;
        }
    } else if constexpr(inputRate == Rate_6_0_Mhz) {
        switch (upsampling) {
                case 6:
                    return run_main_templ<inputRate, Rate_6_0_Mhz, inputFormat>(tapsFile);
                break;
                case 12: 
                    return run_main_templ<inputRate, Rate_12_0_Mhz, inputFormat>(tapsFile);
                break;
                case 16:
                    return run_main_templ<inputRate, Rate_16_0_Mhz, inputFormat>(tapsFile);
                break;
                case 24:
                    return run_main_templ<inputRate, Rate_24_0_Mhz, inputFormat>(tapsFile);
                break;
                default:
                    std::cerr << "Unknown internal sample rate " << upsampling << std::endl;
                    return false;
                break;
                }
    } else if constexpr(compilerInputSampleRate == Rate_10_0_Mhz) { 
        switch (upsampling) {
                case 10:
                    return run_main_templ<inputRate, Rate_10_0_Mhz, inputFormat>(tapsFile);
                break;
                case 20: 
                    return run_main_templ<inputRate, Rate_20_0_Mhz, inputFormat>(tapsFile);
                break;
                case 24:
                    return run_main_templ<inputRate, Rate_24_0_Mhz, inputFormat>(tapsFile); 
                break;
                default:
                    std::cerr << "Unknown internal sample rate " << upsampling << std::endl;
                    return false;
                break;
                }
    } else {
        std::cerr << "Unknown internal sample rate " << upsampling << std::endl;
        return false;
    }
    return false;
}


int main(int argc, char** argv) {
    // Parse command line options.
    int upsampling = (compilerInputSampleRate / 1000000);
    std::string tapsFile = "";
    SampleStreamInputFormat inputFormat = (compilerInputSampleRate == Rate_2_4_Mhz) ? IQ_RTL_SDR : IQ_AIRSPY_RX;
    int opt;
    while ((opt = getopt(argc, argv, "abcdf:rqmu:h")) != -1) {
        switch (opt) {
            case 'a':
            case 'b':
            case 'c':
            case 'd':
                printDeprecated();
                return -1;
                break;
            case 'r':
                inputFormat = IQ_AIRSPY_RX_RAW;
                break;
            case 'q':
                inputFormat = IQ_AIRSPY_RX_RAW_IQ_FILTER;
                break;
            case 'f':
                inputFormat = IQ_AIRSPY_RX_RAW_IQ_FILTER_FILE;
                if (optarg == nullptr || optarg[0] == '-') { 
                    std::cerr << "Error: -f requires a filename\n"; 
                    return -1; 
                }
                tapsFile = optarg;
                break;
            case 'm':
                inputFormat = MAG_FLOAT32;
                break;
            case 'u':
                upsampling = std::atoi(optarg);
                break;
            case 'h':
                printHelp<compilerInputSampleRate>();
                return 0;
            break;
            default:
            return 0;
            break;
        }
    }

    if constexpr(compilerInputSampleRate == Rate_2_4_Mhz) {
        upsampling = 8;
        // standard settings, only distinguish between the input formats
        switch (inputFormat)
        {
        case IQ_RTL_SDR:
            run_main<IQ_RTL_SDR>(upsampling);
            break;
        case MAG_FLOAT32:
            run_main<MAG_FLOAT32>(upsampling);
            break;
        default:
            break;
        }
    } else {
        if (!check_upsampling_parameter<compilerInputSampleRate>(upsampling)) {
            std::cerr << "Invalid -u " << upsampling <<" parameter" << std::endl; 
            return 0;
        };

        switch (inputFormat)
        {
        case IQ_AIRSPY_RX:
            run_main<IQ_AIRSPY_RX>(upsampling);
            break;
        case IQ_AIRSPY_RX_RAW:
            run_main<IQ_AIRSPY_RX_RAW>(upsampling);
            break;
        case IQ_AIRSPY_RX_RAW_IQ_FILTER:
            run_main<IQ_AIRSPY_RX_RAW_IQ_FILTER>(upsampling);
            break;
         case IQ_AIRSPY_RX_RAW_IQ_FILTER_FILE:
            run_main<IQ_AIRSPY_RX_RAW_IQ_FILTER_FILE>(upsampling, tapsFile);
            break;
        case MAG_FLOAT32:
            run_main<MAG_FLOAT32>(upsampling);
            break;
        default:
            break;
        }
    }

    return 0;
}
