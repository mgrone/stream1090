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

class IQLowPass {
    
    public:
        IQLowPass() {
            m_new_index = 0;
            std::fill(std::begin(m_delay_I), std::end(m_delay_I), 0.0f);
            std::fill(std::begin(m_delay_Q), std::end(m_delay_Q), 0.0f);
        }

        

        static constexpr float tap(int i) {
            return IQ_TAPS[i];
        }

        void apply(float& value_I, float& value_Q) {
            // insert the new element at new index pos
            m_delay_I[m_new_index] = value_I;
            m_delay_Q[m_new_index] = value_Q;

            // compute the center index
            int center_index = (m_new_index + halfNumTaps + 1) & 31;

            // deal with this separatly
            float sum_I = m_delay_I[center_index] * tap(halfNumTaps);
            float sum_Q = m_delay_Q[center_index] * tap(halfNumTaps);

            // init i (left) and j (right) at the center
            int i = center_index;
            int j = center_index;

            // we iterate inside out
            for (int k = halfNumTaps-1; k >= 0; k--) {
                i = (i - 1) & 31;
                j = (j + 1) & 31;

                sum_I += tap(k) * (m_delay_I[i] + m_delay_I[j]);
                sum_Q += tap(k) * (m_delay_Q[i] + m_delay_Q[j]);
            }
            
            m_new_index = (m_new_index + 1) & 31;
            value_I = sum_I;
            value_Q = sum_Q;
        }
    private:
        /*static constexpr std::array<float, 25> IQ_TAPS = { 
        -0.000998606272947510f, 0.001695637278417295f, -0.003054430179754289f, 0.005055504379767936f, -0.007901319195893647f,
        0.011873357051047719f, -0.017411159379930066f,  0.025304817427568772f, -0.037225225204559217f, 0.057533286997004301f,
        -0.102327462004259350f, 0.317034472508947400f,  0.500000000000000000f,  0.317034472508947400f, -0.102327462004259350f,
        0.057533286997004301f, -0.037225225204559217f,  0.025304817427568772f, -0.017411159379930066f,  0.011873357051047719f,
        -0.007901319195893647f, 0.005055504379767936f, -0.003054430179754289f,  0.001695637278417295f, -0.000998606272947510f }; 
        */
        
        
        static constexpr std::array<float, 7> IQ_TAPS = { 
             
            
            0.1f,
            -0.1f, 
            0.7f,

            1.0f,

            0.7f, 
            -0.1f,
            0.1f
        }; 
    
        static constexpr size_t numTaps = IQ_TAPS.size();
        static constexpr size_t bufferSize = 32;
        static constexpr size_t halfNumTaps = numTaps / 2;

        float m_delay_I[bufferSize];
        float m_delay_Q[bufferSize];
        int m_new_index = 0;
};
