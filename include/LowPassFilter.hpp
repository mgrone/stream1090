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
#include "CustomFilterTaps.hpp"

template<SampleRate inputRate, SampleRate outputRate>
class IQLowPass {
public:
    IQLowPass() {
        m_new_index = 0;
        std::fill(std::begin(m_delay_I), std::end(m_delay_I), 0.0f);
        std::fill(std::begin(m_delay_Q), std::end(m_delay_Q), 0.0f);
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "[IQLowPass] tap count: " << numTaps << " symmetric: " << areTapsSymmetric << "\n";
        oss << "[IQLowPass] taps: {";
        for (size_t i = 0; i < numTaps; i++) {
            oss << taps[i];
            if (i + 1 < numTaps) {
                oss << ", ";
            }
        }
        oss << "}";
        return oss.str();
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
            sum_I += taps[k] * m_delay_I[j];
            sum_Q += taps[k] * m_delay_Q[j];
        }
    }

    void sum_sym(float& sum_I, float& sum_Q) const {
        constexpr auto halfNumTaps = numTaps >> 1; 
    
        if constexpr(areTapsOdd) {
            // compute the center index
            const int center_index = (m_new_index + halfNumTaps + 1) & (bufferSize-1);
            // deal with this separatly
            sum_I += m_delay_I[center_index] * taps[halfNumTaps];
            sum_Q += m_delay_Q[center_index] * taps[halfNumTaps];
        }

        // init i (left) and j (right)
        int i = m_new_index;
        int j = (m_new_index + numTaps + 1) & (bufferSize-1);

        for (size_t k = 0; k < halfNumTaps; k++) {
            i = (i + 1) & (bufferSize-1);
            j = (j - 1) & (bufferSize-1);

            sum_I += taps[k] * (m_delay_I[i] + m_delay_I[j]);
            sum_Q += taps[k] * (m_delay_Q[i] + m_delay_Q[j]);
        }
    }  

    static constexpr auto taps = LowPassTaps::getCustomTaps<inputRate, outputRate>();
    static constexpr auto numTaps = taps.size();
    static constexpr auto bufferSize = std::bit_ceil(numTaps);
    static constexpr bool areTapsOdd = LowPassTaps::areCustomTapsOdd<inputRate, outputRate>();
    static constexpr bool areTapsSymmetric = LowPassTaps::areCustomTapsSymmetric<inputRate, outputRate>(); 
    
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

    IQLowPassDynamic(const std::vector<float>& taps) 
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
        setTaps(taps);
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "[IQLowPassDynamic] tap count: " << m_numTaps << " symmetric: " << m_areTapsSymmetric << "\n";
        oss << "[IQLowPassDynamic] taps: {";
        for (size_t i = 0; i < m_taps.size(); i++) {
            oss << m_taps[i];
            if (i + 1 < m_numTaps) {
                oss << ", ";
            }
        }
        oss << "}";
        return oss.str();
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

