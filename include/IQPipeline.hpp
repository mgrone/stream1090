/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <tuple>
#include <cmath>
#include <utility>


struct DCRemoval {
    explicit DCRemoval(float alpha = 0.005f)
        : m_alpha(alpha), m_avg_I(0.0f), m_avg_Q(0.0f)
    {}

    inline void apply(float& I, float& Q) noexcept {
        float dI = I - m_avg_I;
        float dQ = Q - m_avg_Q;

        m_avg_I += dI * m_alpha;
        m_avg_Q += dQ * m_alpha;

        I = dI;
        Q = dQ;
    }

    void setAlpha(float alpha) {
        m_alpha = alpha;
    }

    std::string toString() const { 
        std::ostringstream oss; 
        oss << "[DCRemoval] alpha: " << m_alpha; 
        return oss.str(); 
    }
private:
    float m_alpha;
    float m_avg_I;
    float m_avg_Q;
};


struct FlipSigns {
    FlipSigns() = default;

    inline void apply(float& I, float& Q) noexcept {
        if (m_flip) {
            I = -I;
            Q = -Q;
        }
        m_flip = !m_flip;
    }

    std::string toString() const { 
        return "[FlipSigns] enabled"; 
    }
private:
    bool m_flip = false;
};


template<typename... Stages>
class IQPipeline {
public:
    IQPipeline(Stages... stages)
        : m_stages(std::move(stages)...)
    {}

    inline float process(float I, float Q) noexcept {
        // run IQ through the stages
        applyStages(I, Q, std::index_sequence_for<Stages...>{});
        // and compute the magnitude
        return std::sqrt(I * I + Q * Q);
    }

    std::string toString() const {
        return toStringImpl(std::index_sequence_for<Stages...>{});
    }

private:
    // the different stages of the IQ pair pipeline 
    // in the order how they are being executed on each pair
    std::tuple<Stages...> m_stages;

    // runs the stages above on a single pair
    template<std::size_t... Is>
    inline void applyStages(float& I, float& Q, std::index_sequence<Is...>) noexcept {
        // C++ fun: apply(I, Q) on each stage in order
        (std::get<Is>(m_stages).apply(I, Q), ...);
    }

    template<std::size_t... Is>
    std::string toStringImpl(std::index_sequence<Is...>) const {
        std::ostringstream oss;
        ((oss << std::get<Is>(m_stages).toString() << "\n"), ...);
        return oss.str();
    }
};

// makes constructing a custom pipeline quite easy
template<typename... Stages>
auto make_pipeline(Stages&&... stages) {
    return IQPipeline<std::decay_t<Stages>...>(std::forward<Stages>(stages)...);
}

