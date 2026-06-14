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
concept MessageHandler = requires(H h, uint64_t sampleIndex, uint64_t frameShort, const Bits128& frameLong) {
    { h.handleShort(sampleIndex, frameShort) };
    { h.handleLong(sampleIndex, frameLong) };
};

template<typename Sampler>
class StdOutMessageHandler {
public:
    explicit StdOutMessageHandler() : m_writer(std::cout) {}

    void handleShort(uint64_t sampleIndex, const uint64_t frame) {
        const uint64_t MLAT_timeStamp = MLAT::sampleIndexToMlatTime<Sampler::NumStreams>(sampleIndex);
        m_writer.write_short_MLAT(MLAT_timeStamp, frame);
    }

    void handleLong(uint64_t sampleIndex, const Bits128& frame) {
        const uint64_t MLAT_timeStamp = MLAT::sampleIndexToMlatTime<Sampler::NumStreams>(sampleIndex);
        m_writer.write_long_MLAT(MLAT_timeStamp, frame);
    }

    AVRWriter m_writer;
};

template<typename R>
concept RssiProvider = requires(R r) {
    { r.getRSSI() } -> std::convertible_to<uint8_t>;
};

template<typename Sampler, RssiProvider R>
class RssiStdOutMessageHandler {
public:
    explicit RssiStdOutMessageHandler(const R& rssi)
        : m_writer(std::cout), 
          rssiProvider(rssi) {}

    void handleShort(uint64_t sampleIndex, const uint64_t frame) {
        const uint64_t MLAT_timeStamp = MLAT::sampleIndexToMlatTime<Sampler::NumStreams>(sampleIndex);
        const uint8_t rssi = rssiProvider.getRSSI();
        m_writer.write_short_MLAT_RSSI(MLAT_timeStamp, frame, rssi);
    }

    void handleLong(uint64_t sampleIndex, const Bits128& frame) {
        const uint64_t MLAT_timeStamp = MLAT::sampleIndexToMlatTime<Sampler::NumStreams>(sampleIndex);
        const uint8_t rssi = rssiProvider.getRSSI();
        m_writer.write_long_MLAT_RSSI(MLAT_timeStamp, frame, rssi);
    }

private:
    AVRWriter m_writer;
    const R& rssiProvider;
};
