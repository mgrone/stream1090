/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "MainInstance.hpp"

template<typename Tuple, typename F>
constexpr bool for_each_in_tuple(const Tuple& t, F&& f) {
    bool done = false;
    std::apply([&](auto const&... elems) {
        (( !done && f(elems) ? done = true : false ), ...);
    }, t);
    return done;
}

// Runs the first preset in `groupPresets` that matches the requested
// configuration. This is the one place MainInstance<...> gets instantiated,
// so it is called from the per-backend translation units (src/presets_*.cpp)
// to keep each TU's compilation load proportional to its own preset count.
template<typename Tuple>
std::optional<bool> runPresetGroup(const Tuple& groupPresets,
                                   const CompileTimeVars& compileTimeVars,
                                   const RuntimeVars& runtimeVars) {
    std::optional<bool> outcome;
    for_each_in_tuple(groupPresets, [&](auto const& p) {
        using P = std::decay_t<decltype(p)>;

        if (P::RawFormatType::id  == compileTimeVars.rawFormat &&
            P::inputRate          == compileTimeVars.inputRate &&
            P::outputRate         == compileTimeVars.outputRate &&
            P::pipelineOption     == compileTimeVars.pipelineOption)
        {
            outcome = MainInstance<P>(runtimeVars).run();
            return true;
        }
        return false;
    });
    return outcome;
}

#if !defined(STREAM1090_CUSTOM_INPUT) || !STREAM1090_CUSTOM_INPUT
std::optional<bool> runRtlSdrPresets(const CompileTimeVars&, const RuntimeVars&);
std::optional<bool> runAirspyPresets(const CompileTimeVars&, const RuntimeVars&);
#endif