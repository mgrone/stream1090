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

void runFromStdIn(bool isFloat) {
    SampleStream<Sampler_2_4_to_6_0_Mhz> sampleStream;
    
    if (isFloat) {
        sampleStream.read<MAG_float>(std::cin);
    } else { 
        sampleStream.read<IQ_uint8>(std::cin);
    }
}

int main(int argc, char** argv) {
    bool g_readFloats = false;
    // Parse command line options.
    int opt;
    while ((opt = getopt(argc, argv, "f")) != -1) {
        switch (opt) {
            case 'f':
                g_readFloats = true;
                break;
            default:
            return 0;
        }
    }
   
    runFromStdIn(g_readFloats);
    return 0;
}
