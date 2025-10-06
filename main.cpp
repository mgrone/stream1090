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

void run_rtl_sdr() {
    std::cerr << "[Stream1090] Input Sampling Rate: 2.4 Msps. Internal working speed: 8 Msps" << std::endl;
    // create the default rtl_sdr instance 
    SampleStream<Sampler_2_4_to_8_0_Mhz> sampleStream;
    // start reading thhe uint8 iq data from rtl_sdr
    sampleStream.read<IQ_RTL_SDR>(std::cin);
}

void run_airspy_6_to_12() {
    std::cerr << "[Stream1090] Input Sampling Rate: 6 Msps. Internal working speed: 12 Msps" << std::endl;
    // create an instance with a simple 1:2 sampler 
    SampleStream<Sampler_6_0_to_12_0_Mhz> sampleStream;
    // start reading the int16 iq data from airspy_rx
    sampleStream.read<IQ_AIRSPY_RX>(std::cin);
}

void run_airspy_6_to_6() {
    std::cerr << "[Stream1090] Input Sampling Rate: 6 Msps. Internal working speed: 6 Msps (passthrough)" << std::endl;
    // create an instance with a simple 1:1 sampler 
    SampleStream<Sampler_6_0_to_6_0_Mhz> sampleStream;
    // start reading the int16 iq data from airspy_rx
    sampleStream.read<IQ_AIRSPY_RX>(std::cin);
}

void run_airspy_10_to_10() {
    std::cerr << "[Stream1090] Input Sampling Rate: 10 Msps. Internal working speed: 10 Msps (passthrough)" << std::endl;
    // create an instance with a simple 1:2 sampler 
    SampleStream<Sampler_10_0_to_10_0_Mhz> sampleStream;
    // start reading the int16 iq data from airspy_rx
    sampleStream.read<IQ_AIRSPY_RX>(std::cin);
}

void run_airspy_10_to_20() {
    std::cerr << "[Stream1090] Input Sampling Rate: 10 Msps. Internal working speed: 20 Msps" << std::endl;
    // create an instance with a simple 1:1 sampler 
    SampleStream<Sampler_10_0_to_20_0_Mhz> sampleStream;
    // start reading the int16 iq data from airspy_rx
    sampleStream.read<IQ_AIRSPY_RX>(std::cin);
}

void run_float32_mag() {
    // create the default rtl_sdr instance 
    SampleStream<Sampler_2_4_to_8_0_Mhz> sampleStream;
    // start reading floats at 2.4Mhz
    sampleStream.read<MAG_FLOAT32>(std::cin);
}

void printHelp() {
    std::cout << "No parameters: Stream1090 expects standard rtl_sdr output @2.4Mhz. This will then be upsampled to 8 Mhz internally." << std::endl;
    std::cout << "-a : 6Mhz passthrough mode. Stream1090 expects int16 IQ @6Mhz (internal passthrough mode, working speed will remain @6Mhz)" << std::endl;
    std::cout << "-b : 6-to-12Mhz mode. Stream1090 expects int16 IQ data @6Mhz and will then upsample that to 12Mhz internally" << std::endl;
    std::cout << "-c : 10Mhz passthrough mode. Stream1090 expects int16 IQ data @10Mhz (internal passthrough mode, working speed will remain @10Mhz)" << std::endl;
    std::cout << "-d : insane mode. Stream1090 expects int16 IQ data @10Mhz and will then upsample that to 20Mhz internally. " << std::endl;
    std::cout << "-f : stream1090 expects float32 magnitudes at a rate of 2.4Mhz. Resampling then to 8 Mhz." << std::endl;
}

int main(int argc, char** argv) {
    // Parse command line options.
    int inputFormat = 0;
    int opt;
    while ((opt = getopt(argc, argv, "abcdfh")) != -1) {
        switch (opt) {
            case 'a':
                inputFormat = 1;
                break;
            case 'b':
                inputFormat = 3;
                break;
            case 'c':
                inputFormat = 4;
                break;
            case 'd':
                inputFormat = 5;
                break;
            case 'f':
                inputFormat = 2;
                break;
            case 'h':
                printHelp();
                return 0;
            break;
            default:
            break;
        }
    }
   
    switch (inputFormat)
    {
    case 1:
        run_airspy_6_to_6();
        break;
    case 3:
        run_airspy_6_to_12();
        break;
    case 4:
        run_airspy_10_to_10();
        break;
    case 5:
        run_airspy_10_to_20();
        break;
    case 2:
        run_float32_mag();
        break;
    
    default:
        run_rtl_sdr();
        break;
    }
    return 0;
}
