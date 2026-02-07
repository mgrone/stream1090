/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include "LowPassFilterTaps.hpp"

namespace LowPassTaps {

#define STREAM1090_OVERRIDE_TAPS(IN_RATE, OUT_RATE, ...)                           \
template<>                                                                          \
constexpr auto getCustomTaps<IN_RATE, OUT_RATE>() {                                 \
    return std::to_array<float>({ __VA_ARGS__ });                                   \
}

template<SampleRate inputRate, SampleRate outputRate>
constexpr auto getCustomTaps() {
    return LowPassTaps::getTaps<inputRate, outputRate>();
}

// include possible overrides
#include "../custom_filters/OverrideFilterTaps.hpp"

// checks if the taps are symmetric
template<SampleRate inputRate, SampleRate outputRate>
constexpr bool areCustomTapsSymmetric() {
    const auto taps = getCustomTaps<inputRate, outputRate>();
    for (size_t i = 0; i < taps.size() / 2; i++) {
        if (taps[i] != taps[taps.size() - 1 - i])
            return false;
    }
    return true;
}

// checks if the length is odd
template<SampleRate inputRate, SampleRate outputRate>
constexpr bool areCustomTapsOdd() {
    return (getCustomTaps<inputRate, outputRate>().size() % 2) != 0;
}

} // namespace LowPassTaps
