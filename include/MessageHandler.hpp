/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <iostream>
#include "Bits128.hpp"
#include "ModeS.hpp"
#include "AVRWriter.hpp"
#include "ModeSFrame.hpp"
#include "TcpOutputServer.hpp"

template<typename H>
concept MessageHandler = requires(H h, uint64_t sampleIndex, uint64_t frameShort, const Bits128& frameLong) {
    { h.handleShort(sampleIndex, frameShort) };
    { h.handleLong(sampleIndex, frameLong) };
};

template<typename Sampler>
class StdOutMessageHandler {
public:
    explicit StdOutMessageHandler(bool stdoutEnabled = true, TcpOutputServer* tcpServer = nullptr)
        : m_writer(std::cout), m_stdoutEnabled(stdoutEnabled), m_tcpServer(tcpServer) {}

    void handleShort(uint64_t sampleIndex, const uint64_t frame) {
        const uint64_t MLAT_timeStamp = MLAT::sampleIndexToMlatTime<Sampler::NumStreams>(sampleIndex);
        if (m_stdoutEnabled)
            m_writer.write_short_MLAT(MLAT_timeStamp, frame);
        if (m_tcpServer)
            m_tcpServer->tryPublish(ModeSFrame::shortFrame(MLAT_timeStamp, frame, 0, false));
    }

    void handleLong(uint64_t sampleIndex, const Bits128& frame) {
        const uint64_t MLAT_timeStamp = MLAT::sampleIndexToMlatTime<Sampler::NumStreams>(sampleIndex);
        if (m_stdoutEnabled)
            m_writer.write_long_MLAT(MLAT_timeStamp, frame);
        if (m_tcpServer)
            m_tcpServer->tryPublish(ModeSFrame::longFrame(MLAT_timeStamp, frame, 0, false));
    }

    AVRWriter m_writer;
    bool m_stdoutEnabled;
    TcpOutputServer* m_tcpServer;
};

template<typename R>
concept RssiProvider = requires(R r) {
    { r.getRSSIShort() } -> std::convertible_to<uint8_t>;
    { r.getRSSILong() } -> std::convertible_to<uint8_t>;
};

template<typename Sampler, RssiProvider R>
class RssiStdOutMessageHandler {
public:
    explicit RssiStdOutMessageHandler(const R& rssi, bool stdoutEnabled = true, TcpOutputServer* tcpServer = nullptr)
        : m_writer(std::cout),
          rssiProvider(rssi),
          m_stdoutEnabled(stdoutEnabled),
          m_tcpServer(tcpServer) {}

    void handleShort(uint64_t sampleIndex, const uint64_t frame) {
        const uint64_t MLAT_timeStamp = MLAT::sampleIndexToMlatTime<Sampler::NumStreams>(sampleIndex);
        const uint8_t rssi = rssiProvider.getRSSIShort();
        if (m_stdoutEnabled)
            m_writer.write_short_MLAT_RSSI(MLAT_timeStamp, frame, rssi);
        if (m_tcpServer)
            m_tcpServer->tryPublish(ModeSFrame::shortFrame(MLAT_timeStamp, frame, rssi, true));
    }

    void handleLong(uint64_t sampleIndex, const Bits128& frame) {
        const uint64_t MLAT_timeStamp = MLAT::sampleIndexToMlatTime<Sampler::NumStreams>(sampleIndex);
        const uint8_t rssi = rssiProvider.getRSSILong();
        if (m_stdoutEnabled)
            m_writer.write_long_MLAT_RSSI(MLAT_timeStamp, frame, rssi);
        if (m_tcpServer)
            m_tcpServer->tryPublish(ModeSFrame::longFrame(MLAT_timeStamp, frame, rssi, true));
    }

private:
    AVRWriter m_writer;
    const R& rssiProvider;
    bool m_stdoutEnabled;
    TcpOutputServer* m_tcpServer;
};
