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

template<typename H>
concept MessageHandler = requires(H h, int bitStream, uint64_t ts, uint64_t f, const Bits128& b) {
    { h.handleShort(bitStream, ts, f) };
    { h.handleLong(bitStream, ts, b) };
};

class StdOutMessageHandler {
public:
    explicit StdOutMessageHandler() : m_writer(std::cout) {}

    void handleShort(int, uint64_t MLAT_timeStamp, const uint64_t frame) {
        m_writer.write_short_MLAT(MLAT_timeStamp, frame);
    }

    void handleLong(int, uint64_t MLAT_timeStamp, const Bits128& frame) {
        m_writer.write_long_MLAT(MLAT_timeStamp, frame);
    }

    AVRWriter m_writer;
};

template<int NumStreams>
class RawOutputMessageHandler {
public:
    void handleShort(int, uint64_t, const uint64_t frame) {
        ModeS::printFrameShortRaw(std::cout, frame);
    }

    void handleLong(int, uint64_t, const Bits128& frame) {
        ModeS::printFrameLongRaw(std::cout, frame);
    }
};

template<typename R>
concept RssiProvider = requires(R r, int streamIndex) {
    { r.getRSSI(streamIndex) } -> std::convertible_to<uint8_t>;
};

template<RssiProvider R>
class RssiStdOutMessageHandler {
public:
    explicit RssiStdOutMessageHandler(const R& rssi)
        : m_writer(std::cout), 
          rssiProvider(rssi) {}

    void handleShort(int streamIndex, uint64_t MLAT_timeStamp, const uint64_t frame) {
        uint8_t rssi = rssiProvider.getRSSI(streamIndex);
        m_writer.write_short_MLAT_RSSI(MLAT_timeStamp, frame, rssi);
    }

    void handleLong(int streamIndex, uint64_t MLAT_timeStamp, const Bits128& frame) {
        uint8_t rssi = rssiProvider.getRSSI(streamIndex);
        m_writer.write_long_MLAT_RSSI(MLAT_timeStamp, frame, rssi);
    }

private:
    AVRWriter m_writer;
    const R& rssiProvider;
};
