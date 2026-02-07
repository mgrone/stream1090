/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include <array>
#include "Sampler.hpp"

// Tap definitions
namespace LowPassTaps {
    // template for the tap definitions. Returns a canonical 1-element filter as default
    template<SampleRate inputRate>
    constexpr auto getTaps() {
        return std::array<float, 1>{ 1.0f }; 
    }
    
    template<SampleRate inputRate, SampleRate outputRate>
    constexpr auto getTaps() {
        return getTaps<inputRate>(); 
    }

    template<>
    constexpr auto getTaps<Rate_6_0_Mhz>(){
        return std::to_array<float>({ 
            -0.0014986745081841946f,
            0.04620366916060448f,
            0.03521840274333954f,
            0.03104071319103241f,
            0.06415047496557236f,
            0.0026237000711262226f,
            0.17662887275218964f,
            0.2912658154964447f,
            0.17662887275218964f,
            0.0026237000711262226f,
            0.06415047496557236f,
            0.03104071319103241f,
            0.03521840274333954f,
            0.04620366916060448f,
            -0.0014986745081841946f  
        });
    };

    template<>
    constexpr auto getTaps<Rate_6_0_Mhz, Rate_12_0_Mhz>(){
        return std::to_array<float>({ 
            -0.0016630655154585838,
            0.04539839178323746,
            0.03437880426645279,
            0.024560820311307907,
            0.03638293221592903,
            0.020813733339309692,
            0.21404020488262177,
            0.25217634439468384,
            0.21404020488262177,
            0.020813733339309692,
            0.03638293221592903,
            0.024560820311307907,
            0.03437880426645279,
            0.04539839178323746,
            -0.0016630655154585838
         });
    };


    template<>
    constexpr auto getTaps<Rate_6_0_Mhz, Rate_24_0_Mhz>(){
        return std::to_array<float>({ 
            -0.00025101282517425716,
            0.045283835381269455,
            0.01910368911921978,
            0.07402653992176056,
            0.0615474134683609,
            -0.07990248501300812,
            0.23224414885044098,
            0.29589566588401794,
            0.23224414885044098,
            -0.07990248501300812,
            0.0615474134683609,
            0.07402653992176056,
            0.01910368911921978,
            0.045283835381269455,
            -0.00025101282517425716
        });
    };

    template<>
    constexpr auto getTaps<Rate_10_0_Mhz>(){
        return std::to_array<float>({  
            0.010117686353623867f,
            0.04508169740438461f,
            0.01707679219543934f,
            -0.04200827330350876f,
            -0.051508828997612f,
            0.17665232717990875f,
            0.21491391956806183f,
            0.25934934616088867f,
            0.21491391956806183f,
            0.17665232717990875f,
            -0.051508828997612f,
            -0.04200827330350876f,
            0.01707679219543934f,
            0.04508169740438461f,
            0.010117686353623867f,
        });
    };
    

    template<>
    constexpr auto getTaps<Rate_10_0_Mhz, Rate_24_0_Mhz>(){
        return std::to_array<float>({    
            -0.008151337504386902,
            0.07549969106912613,
            0.10143674910068512,
            -0.047200191766023636,
            -0.1384957879781723,
            0.17454688251018524,
            0.3371904194355011,
            0.01034723874181509,
            0.3371904194355011,
            0.17454688251018524,
            -0.1384957879781723,
            -0.047200191766023636,
            0.10143674910068512,
            0.07549969106912613,
            -0.008151337504386902,
        });
    };  
} // end tap definitions