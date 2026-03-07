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

template<typename H>
concept MessageHandler = requires(H h, int bitStream, uint64_t ts, uint64_t f, const Bits128& b) {
    { h.handleShort(bitStream, ts, f) };
    { h.handleLong(bitStream, ts, b) };
};

template<int NumStreams>
class StdOutMessageHandler {
public:
    void handleShort(int, uint64_t timeStamp, const uint64_t frame) {
        ModeS::printFrameShortMLAT(std::cout,  MLAT::sampleIndexToMlatTime<NumStreams>(timeStamp), frame);
    }

    void handleLong(int, uint64_t timeStamp, const Bits128& frame) {
        ModeS::printFrameLongMLAT(std::cout, MLAT::sampleIndexToMlatTime<NumStreams>(timeStamp), frame);
    }
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