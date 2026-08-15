/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <tuple>
#include <cmath>
#include <cstdint>
#include <utility>
#include "RawInputFormat.hpp"


/*
 * Leaky integrator, in fixed point. The accumulator carries the running mean
 * shifted up by m_shift, so the update is an add and the mean is a shift back
 * down, which makes the smoothing factor exactly a negative power of two.
 * setAlpha() therefore snaps to the nearest such factor: the default 0.005
 * becomes 1/256.
 */
struct DCRemoval {
    explicit DCRemoval(float alpha = 0.005f)
        : m_shift(shiftForAlpha(alpha)), m_acc_I(0), m_acc_Q(0)
    {}

    void apply(int16_t& I, int16_t& Q) noexcept {
        const int32_t dI = int32_t(I) - (m_acc_I >> m_shift);
        const int32_t dQ = int32_t(Q) - (m_acc_Q >> m_shift);

        m_acc_I += dI;
        m_acc_Q += dQ;

        I = int16_t(dI);
        Q = int16_t(dQ);
    }

    void applyBlock(int16_t* __restrict I, int16_t* __restrict Q, size_t n) noexcept {
        for (size_t i = 0; i < n; i++)
            apply(I[i], Q[i]);
    }

    void setAlpha(float alpha) noexcept {
        m_shift = shiftForAlpha(alpha);
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "[DCRemoval] alpha: 1/" << (1 << m_shift);
        return oss.str();
    }
private:
    // nearest shift k with 2^-k closest to alpha in the log domain
    static int shiftForAlpha(float alpha) noexcept {
        if (!(alpha > 0.0f)) return 8;
        int k = int(std::lround(std::log2(1.0f / alpha)));
        if (k < 1)  k = 1;
        if (k > 20) k = 20;
        return k;
    }

    int m_shift;
    int32_t m_acc_I;
    int32_t m_acc_Q;
};


struct FlipSigns {
    FlipSigns() = default;

    void apply(int16_t& I, int16_t& Q) noexcept {
        if (m_flip) {
            I = int16_t(-I);
            Q = int16_t(-Q);
        }
        m_flip = !m_flip;
    }

    // Every second sample gets negated, so over a block that is a plain stride
    // two walk instead of a branch per sample.
    void applyBlock(int16_t* __restrict I, int16_t* __restrict Q, size_t n) noexcept {
        for (size_t i = m_flip ? 0 : 1; i < n; i += 2) {
            I[i] = int16_t(-I[i]);
            Q[i] = int16_t(-Q[i]);
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
    IQPipeline(const Stages& ... stages)
        : m_stages(std::move(stages)...)
    {}

    int32_t process(int16_t I, int16_t Q) noexcept {
        // run IQ through the stages
        applyStages(I, Q, std::index_sequence_for<Stages...>{});
        // and compute the squared magnitude
        return int32_t(I) * int32_t(I) + int32_t(Q) * int32_t(Q);
    }

    // Runs the stages without taking the magnitude. The caller can then do the
    // magnitude for a whole block at once, which vectorizes, while the stages
    // themselves carry state from sample to sample and cannot.
    void applyStages(int16_t& I, int16_t& Q) noexcept {
        applyStages(I, Q, std::index_sequence_for<Stages...>{});
    }

    // Same, but stage by stage over a whole block. Every stage walks the block
    // in order and only its own state carries over, so running one stage to
    // completion before starting the next gives the same numbers as running the
    // whole chain per sample. Stages that offer applyBlock() get to filter the
    // block their own way, which is where the FIR wins.
    void applyStagesBlock(int16_t* __restrict I, int16_t* __restrict Q, size_t n) noexcept {
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
    void applyStages(int16_t& I, int16_t& Q, std::index_sequence<Is...>) noexcept {
        // C++ fun: apply(I, Q) on each stage in order
        (std::get<Is>(m_stages).apply(I, Q), ...);
    }

    template<typename Stage>
    static void runStageOnBlock(Stage& stage, int16_t* I, int16_t* Q, size_t n) noexcept {
        if constexpr (requires { stage.applyBlock(I, Q, n); }) {
            stage.applyBlock(I, Q, n);
        } else {
            for (size_t i = 0; i < n; i++)
                stage.apply(I[i], Q[i]);
        }
    }

    template<std::size_t... Is>
    void applyStagesBlock(int16_t* I, int16_t* Q, size_t n, std::index_sequence<Is...>) noexcept {
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

