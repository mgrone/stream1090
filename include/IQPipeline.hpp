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

    void apply(float& I, float& Q) noexcept {
        float dI = I - m_avg_I;
        float dQ = Q - m_avg_Q;

        m_avg_I += dI * m_alpha;
        m_avg_Q += dQ * m_alpha;

        I = dI;
        Q = dQ;
    }

    void setAlpha(float alpha) noexcept {
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

    void apply(float& I, float& Q) noexcept {
        if (m_flip) {
            I = -I;
            Q = -Q;
        }
        m_flip = !m_flip;
    }

    // Every second sample gets negated, so over a block that is a plain stride
    // two walk instead of a branch per sample.
    void applyBlock(float* __restrict I, float* __restrict Q, size_t n) noexcept {
        for (size_t i = m_flip ? 0 : 1; i < n; i += 2) {
            I[i] = -I[i];
            Q[i] = -Q[i];
        }
        m_flip ^= (n & 1) != 0;
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

    float process(float I, float Q) noexcept {
        // run IQ through the stages
        applyStages(I, Q, std::index_sequence_for<Stages...>{});
        // and compute the magnitude
        return std::sqrt(I * I + Q * Q);
    }

    // Runs the stages without taking the magnitude. The caller can then do the
    // magnitude for a whole block at once, which vectorizes, while the stages
    // themselves carry state from sample to sample and cannot.
    void applyStages(float& I, float& Q) noexcept {
        applyStages(I, Q, std::index_sequence_for<Stages...>{});
    }

    // Same, but stage by stage over a whole block. Every stage walks the block
    // in order and only its own state carries over, so running one stage to
    // completion before starting the next gives the same numbers as running the
    // whole chain per sample. Stages that offer applyBlock() get to filter the
    // block their own way, which is where the FIR wins.
    void applyStagesBlock(float* __restrict I, float* __restrict Q, size_t n) noexcept {
        applyStagesBlock(I, Q, n, std::index_sequence_for<Stages...>{});
    }

    // true when there is nothing to run at all, so callers can skip a pass
    static constexpr bool isEmpty = (sizeof...(Stages) == 0);

    std::string toString() const {
        return toStringImpl(std::index_sequence_for<Stages...>{});
    }

private:
    // the different stages of the IQ pair pipeline 
    // in the order how they are being executed on each pair
    std::tuple<Stages...> m_stages;

    // runs the stages above on a single pair
    template<std::size_t... Is>
    void applyStages(float& I, float& Q, std::index_sequence<Is...>) noexcept {
        // C++ fun: apply(I, Q) on each stage in order
        (std::get<Is>(m_stages).apply(I, Q), ...);
    }

    template<typename Stage>
    static void runStageOnBlock(Stage& stage, float* I, float* Q, size_t n) noexcept {
        if constexpr (requires { stage.applyBlock(I, Q, n); }) {
            stage.applyBlock(I, Q, n);
        } else {
            for (size_t i = 0; i < n; i++)
                stage.apply(I[i], Q[i]);
        }
    }

    template<std::size_t... Is>
    void applyStagesBlock(float* I, float* Q, size_t n, std::index_sequence<Is...>) noexcept {
        (runStageOnBlock(std::get<Is>(m_stages), I, Q, n), ...);
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

