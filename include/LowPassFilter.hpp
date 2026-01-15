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

    // the taps for 6 Msps input
    template<>
    constexpr auto getTaps<Rate_6_0_Mhz>(){
        return std::array<float, 7>{ 
            0.22179523f, 0.43102872f, 0.21158125f, -0.00878011f, 0.04563714f, 0.01970692f, 0.07903092f 
        };
    };

    // the taps for 10 Msps
    template<>
    constexpr auto getTaps<Rate_10_0_Mhz>(){
        return std::array<float, 15>{ 
            -0.00346601f, 0.01792696f, 0.04689825f, -0.01513778f, -0.05571929f, 0.1544143f,
            0.262295f, 0.18557717f, 0.262295f, 0.1544143f, -0.05571929f, -0.01513778f,
            0.04689825f, 0.01792696f, -0.00346601 };
    };
    
    // checks if the taps are symmetric
    template<SampleRate inputRate>
    constexpr bool areTapsSymmetric() {
        const auto taps = getTaps<inputRate>(); 
        for (size_t i = 0; i < taps.size() / 2; i++) {
            if (taps[i] != taps[taps.size() - 1 - i])
                return false;
        }
        return true;
    }

    // and if the length is odd
    template<SampleRate inputRate>
    constexpr bool areTapsOdd() {
        return (getTaps<inputRate>().size() % 2) != 0;
    }
} // end tap definitions


template<SampleRate inputRate>
class IQLowPassAsym {
public:
    IQLowPassAsym() {
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
        if constexpr(LowPassTaps::areTapsSymmetric<inputRate>()) {
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
            sum_I += IQ_TAPS[k] * m_delay_I[j];
            sum_Q += IQ_TAPS[k] * m_delay_Q[j];
            j = (j - 1) & (bufferSize - 1);
        }
    }

    void sum_sym(float& sum_I, float& sum_Q) const {
        constexpr auto halfNumTaps = numTaps >> 1; 
    
        if constexpr(LowPassTaps::areTapsOdd<inputRate>()) {
            // compute the center index
            int center_index = (m_new_index + halfNumTaps + 1) & (bufferSize-1);
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

    static constexpr auto numTaps = LowPassTaps::getTaps<inputRate>().size();
    static constexpr std::array<float, numTaps> IQ_TAPS = LowPassTaps::getTaps<inputRate>();
    static constexpr auto bufferSize = std::bit_ceil(numTaps);

    float m_delay_I[bufferSize];
    float m_delay_Q[bufferSize];
    int m_new_index;
};

