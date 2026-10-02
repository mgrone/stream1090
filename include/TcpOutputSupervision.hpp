/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include "Global.hpp"
#include "Logger.hpp"
#include "TcpOutputServer.hpp"

#include <memory>

// Supervision of the optional TCP output, shared by the two run paths.
//
// The policy: an output the user explicitly asked for that fails for good ends
// the run with a failure, even when stdout is still enabled. Otherwise the
// process keeps decoding into nothing and systemd sees no exit to restart.
// A slow client, a full queue or a transient accept() error is not a failure
// of the server, never latches failed(), and therefore never gets here.

// Watchdog-cadence check for the device path. Returns true when the run has to
// be brought down: the reader is woken so the blocking DSP loop returns, and
// the usual shutdown flag is raised.
//
// The device is deliberately not closed here. That belongs to the owner's
// cleanup path, which closes it in every case; closing from this thread as
// well means two threads inside close() on the same handle.
template <typename Device>
inline bool superviseTcpOutput(const TcpOutputServer* server, Device& device) {
    if (server == nullptr || !server->failed())
        return false;
    Log::error("Watchdog") << "The TCP output failed permanently. Initiating shutdown.";
    device.shutdownWriter();
    ProcessSignals::handle_sigint(0);
    return true;
}

// Owner-side completion, called once the producer has quiesced. stop() joins
// the worker, so the state is read again afterwards: a failure that happened
// concurrently with a normal EOF or shutdown would otherwise be missed.
// Returns what the run has to report: false only for a real failure, never for
// a normal stop.
inline bool finishTcpOutput(const std::unique_ptr<TcpOutputServer>& server) {
    if (!server)
        return true;
    server->stop();
    if (!server->failed())
        return true;
    Log::error("TCP") << "The TCP output failed permanently; the run ends with a failure.";
    return false;
}
