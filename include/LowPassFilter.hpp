/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once
#include <numeric>
#include <array>
#include <cstddef>
#include "Sampler.hpp"


namespace LowPassFilter {
    
    constexpr size_t getNumTaps() { return 31; };

    template<SampleRate inputRate>
    static constexpr auto getTaps();

    // best_filter_6M_1
    template<>
    constexpr auto getTaps<Rate_6_0_Mhz>() {
        return std::array<float, getNumTaps()>{ 
            0.04691808f, -0.02944228f,  0.02481813f,  0.00687245f, -0.03778376f, -0.05536104f,
            -0.03637546f, -0.06929483f,  0.04111258f, -0.0142561f,  -0.05956734f, -0.00396889f,
            -0.04647978f, -0.06260861f,  0.38121662f,  0.8284003f,   0.38121662f, -0.06260861f,
            -0.04647978f, -0.00396889f, -0.05956734f, -0.0142561f,   0.04111258f, -0.06929483f,
            -0.03637546f, -0.05536104f, -0.03778376f,  0.00687245f,  0.02481813f, -0.02944228f,
            0.04691808f }; 
    }

    // best_filter_6M_2
    /* template<>
    constexpr auto getTaps<Rate_6_0_Mhz>() {
        return std::array<float, getNumTaps()>{ 
            -1.52463711e-02f,  3.94902828e-03f,  5.83078951e-05f,  4.56780882e-03f,
            -1.80472443e-02f, -4.49779052e-03f, -7.45157253e-03f, -1.28041669e-02f,
            2.82423400e-03f,  4.68200623e-02f,  7.34374402e-03f,  2.66519989e-02f,
            -6.76337348e-03f,  8.24102969e-02f,  2.44663387e-01f,  2.91043301e-01f,
            2.44663387e-01f,  8.24102969e-02f, -6.76337348e-03f,  2.66519989e-02f,
            7.34374402e-03f,  4.68200623e-02f,  2.82423400e-03f, -1.28041669e-02f,
            -7.45157253e-03f, -4.49779052e-03f, -1.80472443e-02f,  4.56780882e-03f,
            5.83078951e-05f,  3.94902828e-03f, -1.52463711e-02f }; 
    } */

    // best_filter_10M_2
    template<>
    constexpr auto getTaps<Rate_10_0_Mhz>() {
        return std::array<float, getNumTaps()>{ 
            0.00055077f, -0.01847956f,  0.00234699f, -0.01789507f,  0.00318175f,  0.05594195f, 
            0.01237755f, -0.06771679f, 0.05199363f, -0.02546499f, 0.16795284f, -0.07870515f,
            -0.16818146f, 0.2712337f, 0.2018848f, 0.21795812f, 0.2018848f, 0.2712337f,
            -0.16818146f, -0.07870515f,  0.16795284f, -0.02546499f,  0.05199363f, -0.06771679f, 
            0.01237755f,  0.05594195f,  0.00318175f, -0.01789507f, 0.00234699f, -0.01847956f, 
            0.00055077 }; 
    }

    // best_filter_10M_3
    /*template<>
    constexpr auto getTaps<Rate_10_0_Mhz>() {
        return std::array<float, getNumTaps()>{ 
            -0.04380234f, -0.04754855f, -0.04717134f, -0.04788126f, -0.11601763f,  0.0854304f,
            0.0507884f,  -0.14951785f,  0.05291474f, -0.02321622f,  0.0626284f,  -0.06690682f,
            -0.28698578f,  0.41696212f,  0.4620927f,   0.39646173f,  0.4620927f,   0.41696212f,
            -0.28698578f, -0.06690682f,  0.0626284f,  -0.02321622f,  0.05291474f, -0.14951785f,
            0.0507884f,   0.0854304f,  -0.11601763f, -0.04788126f, -0.04717134f, -0.04754855f,
            -0.04380234f }; 
    }*/
}