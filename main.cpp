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
    // create the default rtl_sdr instance 
    SampleStream<Sampler_2_4_to_8_0_Mhz> sampleStream;
    // start reading thhe uint8 iq data from rtl_sdr
    sampleStream.read<IQ_RTL_SDR>(std::cin);
}

void run_airspy() {
    // create an instance with a simple 1:1 sampler 
    SampleStream<Sampler_6_0_to_6_0_Mhz> sampleStream;
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
    std::cout << "No parameters: stream1090 expects standard rtl_sdr output with a sample rate of 2.4Mhz" << std::endl;
    std::cout << "-a : experimental airspy mode. stream1090 expects int16 IQ data with a sample rate of 6Mhz" << std::endl;
    std::cout << "-f : stream1090 expects float32 magnitudes at a rate of 2.4Mhz. " << std::endl;
}

int main(int argc, char** argv) {
    // Parse command line options.
    int inputFormat = 0;
    int opt;
    while ((opt = getopt(argc, argv, "afh")) != -1) {
        switch (opt) {
            case 'a':
                inputFormat = 1;
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
        run_airspy();
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
