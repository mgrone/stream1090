/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include <atomic>
#include <csignal>

// global signal for shutting down everything gracefully
static std::atomic<bool> g_shutdownRequested{false};

inline bool shutdownRequested() {
    return g_shutdownRequested.load(std::memory_order_relaxed);
} 

static void handle_sigint(int) {
    g_shutdownRequested.store(true, std::memory_order_relaxed);
}

void install_signal_handlers() {
    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);
}
