/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include <atomic>
#include <csignal>

struct GlobalOptions {
    #ifdef STATS_ENABLED
        static constexpr bool StatsEnabled = (STATS_ENABLED != 0);
    #else
        static constexpr bool StatsEnabled = false;
    #endif

    #ifdef STATS_END_ONLY
        static constexpr bool StatsAtTheEndOnly = StatsEnabled && (STATS_END_ONLY != 0);
    #else
        static constexpr bool StatsAtTheEndOnly = false;
    #endif

    #ifdef STREAM1090_OUTPUT_RAW
        static constexpr bool OutputRawEnabled = (STREAM1090_OUTPUT_RAW != 0);
    #else
        static constexpr bool OutputRawEnabled = false;
    #endif

    #ifdef STREAM1090_CUSTOM_INPUT
        static constexpr bool CustomInputMode = (STREAM1090_CUSTOM_INPUT != 0);
    #else
        static constexpr bool CustomInputMode = false;
    #endif

    #ifdef STREAM1090_RSSI
        static constexpr bool RSSIEnabled = (STREAM1090_RSSI != 0);
    #else
        static constexpr bool RSSIEnabled = false;
    #endif

    #ifdef STREAM1090_HAVE_RTLSDR
        static constexpr bool NativeRtlSdrSupport = (STREAM1090_HAVE_RTLSDR != 0);
    #else
        static constexpr bool NativeRtlSdrSupport = false;
    #endif

    #ifdef STREAM1090_HAVE_AIRSPY
        static constexpr bool NativeAirspySupport = (STREAM1090_HAVE_AIRSPY != 0);
    #else
        static constexpr bool NativeAirspySupport = false;
    #endif
};

namespace ProcessSignals {

    static std::atomic<bool> g_shutdownRequested{false};

    inline bool shutdownRequested() {
        return g_shutdownRequested.load(std::memory_order_relaxed);
    }

    static void handle_sigint(int) {
        g_shutdownRequested.store(true, std::memory_order_relaxed);
    }

    inline void install() {
        std::signal(SIGINT, handle_sigint);
        std::signal(SIGTERM, handle_sigint);
    }

} // end of namespace

