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
         };
    };


    template<>
    constexpr auto getTaps<Rate_6_0_Mhz, Rate_24_0_Mhz>(){
        return std::array<float, 15>{
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
        };
    };

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

    void printTabs() {
        std::cerr << "Sym: " << areTapsSymmetric << std::endl;
        std::cerr << "Odd: " << areTapsOdd << std::endl;
        std::cerr << "Num: " << numTaps << std::endl;
        for (size_t i = 0; i < numTaps; i++) {
            std::cerr << IQ_TAPS[i] << std::endl;
        }
    }

    void apply(float& value_I, float& value_Q) {
        m_delay_I[m_new_index] = value_I;
        m_delay_Q[m_new_index] = value_Q;

        float sum_I = 0.0f;
        float sum_Q = 0.0f;

        // we check at compile time how we sum up
        if constexpr(areTapsSymmetric) {
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
    
        if constexpr(areTapsOdd) {
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

    static constexpr auto IQ_TAPS = LowPassTaps::getTaps<inputRate, outputRate>();
    static constexpr auto numTaps = IQ_TAPS.size();
    static constexpr auto bufferSize = std::bit_ceil(numTaps);
    static constexpr bool areTapsOdd = (numTaps % 2) != 0;
    static constexpr bool areTapsSymmetric = []{ 
        for (size_t i = 0; i < numTaps / 2; ++i) { 
            if (IQ_TAPS[i] != IQ_TAPS[numTaps - 1 - i]) 
            return false; } 
        return true; }();
    

    float m_delay_I[bufferSize];
    float m_delay_Q[bufferSize];
    int m_new_index;
};


template<size_t MaxNumTaps = 64>
class IQLowPassDynamic {
public:
    IQLowPassDynamic() 
        :   m_numTaps(1),
            m_areTapsSymmetric(true),
            m_areTapsOdd(true),
            m_new_index(0) 
    {
        // set all arrays to 0.0f
        std::fill(std::begin(m_taps), std::end(m_taps), 0.0f);
        std::fill(std::begin(m_delay_I), std::end(m_delay_I), 0.0f);
        std::fill(std::begin(m_delay_Q), std::end(m_delay_Q), 0.0f);
        // default is instant response pass through, i.e., 1 tap being 1.0f
        m_taps[0] = 1.0f; 
    }

     void printTabs() {
        std::cerr << "Sym: " << m_areTapsSymmetric << std::endl;
        std::cerr << "Odd: " << m_areTapsOdd << std::endl;
        std::cerr << "Num: " << numTaps() << std::endl;
        for (size_t i = 0; i < numTaps(); i++) {
            std::cerr << m_taps[i] << std::endl;
        }
    }

    inline bool setTaps(const std::vector<float>& newTaps) {
        if (newTaps.size() <= maxNumTaps()) {
            // copy the new values
            std::copy(newTaps.begin(), newTaps.end(), m_taps.begin());
            // get the new number of tabs
            m_numTaps = newTaps.size();
            // check if we have an odd number of taps
            m_areTapsOdd = (m_numTaps % 2) != 0;
            // check if they are symmetric, regardless of if the number is odd or even
            m_areTapsSymmetric = true;
            for (size_t i = 0; i < numTaps() / 2; i++) {
                // compare the opposing elements
                if (m_taps[i] != m_taps[numTaps() - 1 - i]) {
                    // one es enough to break symmetry
                    m_areTapsSymmetric = false;
                    break;
                }
            }
            //printTabs();
            return true;
        }
        return false;
    }

    inline bool loadFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        std::vector<float> taps;
        taps.reserve(MaxNumTaps);

        std::string line;
        while (std::getline(file, line)) {
            // trim whitespace
            if (line.empty()) continue;

            // skip comments
            if (line[0] == '#') continue;

            // parse float
            try {
                float v = std::stof(line);
                taps.push_back(v);
            } catch (...) {
                // malformed line
                return false;
            }

            // too many taps
            if (taps.size() > MaxNumTaps) {
                return false;
            }
        }

        // must have at least one tap
        if (taps.empty()) {
            return false;
        }
#if defined(STATS_ENABLED) && STATS_ENABLED
        std::cerr << "[Stream1090] Loaded " << taps.size() << " taps from " << filename << std::endl;
#endif
        return setTaps(taps);
    }


    // returns the maximum number of taps. 
    // This is a compile time constant and is 64 by default.
    inline size_t maxNumTaps() const {
        return MaxNumTaps;
    }

    // returns the actual number of taps that are in use.
    inline size_t numTaps() const {
        return m_numTaps;
    }

    // applies the FIR to the I and Q values.
    inline void apply(float& value_I, float& value_Q) {
        m_delay_I[m_new_index] = value_I;
        m_delay_Q[m_new_index] = value_Q;

        float sum_I = 0.0f;
        float sum_Q = 0.0f;

        // we check at run time how we sum up
        if (m_areTapsSymmetric) {
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
    inline void sum_not_sym(float& sum_I, float& sum_Q) const {
        // index that wraps around the ring buffer
        int j = m_new_index;
        for (size_t k = 0; k < numTaps(); k++) {
            j = (j + 1) & (bufferSize - 1);
            sum_I += m_taps[k] * m_delay_I[j];
            sum_Q += m_taps[k] * m_delay_Q[j];
        }
    }

    inline void sum_sym(float& sum_I, float& sum_Q) const {
        // half the number of taps
        const auto halfNumTaps = numTaps() >> 1; 
    
        if (m_areTapsOdd) {
            // compute the center index
            const int center_index = (m_new_index + halfNumTaps + 1) & (bufferSize-1);
            // deal with this separatly
            sum_I += m_delay_I[center_index] * m_taps[halfNumTaps];
            sum_Q += m_delay_Q[center_index] * m_taps[halfNumTaps];
        }

        // init i (left) and j (right)
        int i = m_new_index;
        int j = (m_new_index + numTaps() + 1) & (bufferSize-1);

        for (size_t k = 0; k < halfNumTaps; k++) {
            i = (i + 1) & (bufferSize-1);
            j = (j - 1) & (bufferSize-1);

            sum_I += m_taps[k] * (m_delay_I[i] + m_delay_I[j]);
            sum_Q += m_taps[k] * (m_delay_Q[i] + m_delay_Q[j]);
        }
    }  

    // the number of taps currently used
    size_t m_numTaps;

    // flag indicating if the taps are symmetric
    bool m_areTapsSymmetric;

    // flag indicating if the number of taps is even or odd
    bool m_areTapsOdd;

    // size of the delay buffers is the smallest power of 2 with >= maxNumTaps 
    static constexpr auto bufferSize = std::bit_ceil(MaxNumTaps);

    // buffer for the taps
    std::array<float, MaxNumTaps> m_taps;

    // and the delay buffers for I and Q values
    std::array<float, bufferSize> m_delay_I;
    std::array<float, bufferSize> m_delay_Q;

    // index where a new I and Q values are stored in the delay buffers
    int m_new_index;
};

