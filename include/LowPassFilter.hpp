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
#include <bit>
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
        return std::array<float, 15>{ 
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
        };
    };

    template<>
    constexpr auto getTaps<Rate_6_0_Mhz, Rate_12_0_Mhz>(){
        return std::array<float, 15>{ 
            -0.0013338270364329219f,
            0.04304022341966629f,
            0.03304946795105934f,
            0.05120916664600372f,
            0.06323479115962982f,
            -0.03882506489753723f,
            0.18562690913677216f,
            0.32799673080444336f,
            0.18562690913677216f,
            -0.03882506489753723f,
            0.06323479115962982f,
            0.05120916664600372f,
            0.03304946795105934f,
            0.04304022341966629f,
            -0.0013338270364329219f
        };
    };

    template<>
    constexpr auto getTaps<Rate_6_0_Mhz, Rate_24_0_Mhz>(){
        return std::array<float, 15>{
            -0.00025101282517425716f,
            0.045283835381269455f,
            0.01910368911921978f,
            0.07402653992176056f,
            0.0615474134683609f,
            -0.07990248501300812f,
            0.23224414885044098f,
            0.29589566588401794f,
            0.23224414885044098f,
            -0.07990248501300812f,
            0.0615474134683609f,
            0.07402653992176056f,
            0.01910368911921978f,
            0.045283835381269455f,
            -0.00025101282517425716f
        };
    };

    /*template<>
    constexpr auto getTaps<Rate_10_0_Mhz>(){
        return std::array<float, 15>{ 
            -0.00437029f, 
            0.04948019f, 
            0.06041661f, 
            -0.04184288f, 
            -0.04630635f, 
            0.12882623f, 
            0.24776617f, 
            0.2120607f, 
            0.24776617f, 
            0.12882623f, 
            -0.04630635f, 
            -0.04184288f, 
            0.06041661f, 
            0.04948019f, 
            -0.00437029f 
        };
    };*/ 

    template<>
    constexpr auto getTaps<Rate_10_0_Mhz>(){
        return std::array<float, 15>{ 
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
        };
    };
    

    
    template<>
    constexpr auto getTaps<Rate_10_0_Mhz, Rate_24_0_Mhz>(){
        return std::array<float, 15>{
            -0.00725885,  
            0.07273609,  
            0.09169278, 
            -0.06036083, 
            -0.11653714,  
            0.17803416,
            0.31279367,  
            0.05780025,  
            0.31279367,  
            0.17803416, 
            -0.11653714, 
            -0.06036083,
            0.09169278,  
            0.07273609, 
            -0.00725885  
        };
    }; 


    
    // checks if the taps are symmetric
    template<SampleRate inputRate, SampleRate outputRate>
    constexpr bool areTapsSymmetric() {
        const auto taps = getTaps<inputRate, outputRate>(); 
        for (size_t i = 0; i < taps.size() / 2; i++) {
            if (taps[i] != taps[taps.size() - 1 - i])
                return false;
        }
        return true;
    }

    // and if the length is odd
    template<SampleRate inputRate, SampleRate outputRate>
    constexpr bool areTapsOdd() {
        return (getTaps<inputRate, outputRate>().size() % 2) != 0;
    }
} // end tap definitions


template<SampleRate inputRate, SampleRate outputRate>
class IQLowPass {
public:
    IQLowPass() {
        m_new_index = 0;
        std::fill(std::begin(m_delay_I), std::end(m_delay_I), 0.0f);
        std::fill(std::begin(m_delay_Q), std::end(m_delay_Q), 0.0f);
    }

    void apply(float& value_I, float& value_Q) {
        m_delay_I[m_new_index] = value_I;
        m_delay_Q[m_new_index] = value_Q;

        float sum_I = 0.0f;
        float sum_Q = 0.0f;

        // we check at compile time how we sum up
        if constexpr(LowPassTaps::areTapsSymmetric<inputRate, outputRate>()) {
            // regardless of the length, take the quick way
            sum_sym(sum_I, sum_Q);        
        } else {
            // if they are not symmetric, we have to iterate over all taps
            sum_not_sym(sum_I, sum_Q);
        }

        m_new_index = (m_new_index + 1) & (bufferSize - 1);
        value_I = sum_I;
        value_Q = sum_Q;
    }

private:
    void sum_not_sym(float& sum_I, float& sum_Q) const {
        // index that wraps around the ring buffer
        int j = m_new_index;
        for (size_t k = 0; k < numTaps; k++) {
            j = (j + 1) & (bufferSize - 1);
            sum_I += IQ_TAPS[k] * m_delay_I[j];
            sum_Q += IQ_TAPS[k] * m_delay_Q[j];
        }
    }

    void sum_sym(float& sum_I, float& sum_Q) const {
        constexpr auto halfNumTaps = numTaps >> 1; 
    
        if constexpr(LowPassTaps::areTapsOdd<inputRate, outputRate>()) {
            // compute the center index
            const int center_index = (m_new_index + halfNumTaps + 1) & (bufferSize-1);
            // deal with this separatly
            sum_I += m_delay_I[center_index] * IQ_TAPS[halfNumTaps];
            sum_Q += m_delay_Q[center_index] * IQ_TAPS[halfNumTaps];
        }

        // init i (left) and j (right)
        int i = m_new_index;
        int j = (m_new_index + numTaps + 1) & (bufferSize-1);

        for (size_t k = 0; k < halfNumTaps; k++) {
            i = (i + 1) & (bufferSize-1);
            j = (j - 1) & (bufferSize-1);

            sum_I += IQ_TAPS[k] * (m_delay_I[i] + m_delay_I[j]);
            sum_Q += IQ_TAPS[k] * (m_delay_Q[i] + m_delay_Q[j]);
        }
    }  

    static constexpr auto numTaps = LowPassTaps::getTaps<inputRate, outputRate>().size();
    static constexpr std::array<float, numTaps> IQ_TAPS = LowPassTaps::getTaps<inputRate, outputRate>();
    static constexpr auto bufferSize = std::bit_ceil(numTaps);

    float m_delay_I[bufferSize];
    float m_delay_Q[bufferSize];
    int m_new_index;
};

