/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "Bits128.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>

namespace ModeS {

	constexpr inline uint32_t extractICAOWithCA_Short(const uint64_t& frameShort) {
		return ((frameShort >> 24) & 0x7ffffffull);
	}

	constexpr inline uint32_t extractICAOWithCA_Long(const Bits128& frameLong) {
		return ((frameLong.high() >> 16) & (0x7ffffffull));
	}

	inline void printFrameShortMLAT(std::ostream& out, uint64_t timeStamp, const uint64_t& frameShort) {
		// timestamp + frame = @ + 48 bit timestamp + 56 bit frame
		out << '@' << std::hex << std::setfill('0') << std::setw(12) << (timeStamp & 0xffffffffffffull) << std::setw(14) << (frameShort & 0xffffffffffffffull) << ";" << std::endl;
	}

	inline void printFrameLongMLAT(std::ostream& out, uint64_t timeStamp, const Bits128& frame) {
		// timestamp + frame = @ + 48 bit timestamp + 48 + 64 = 112 bit frame
		out << '@' << std::hex << std::setfill('0') << std::setw(12) << (timeStamp & 0xffffffffffffull) << (frame.high() & 0xffffffffffffull) << std::setw(16) << frame.low() << ";" << std::endl;
	}

	inline uint64_t currentTimestamp() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now().time_since_epoch()
			   ).count();
	}

	inline void printFrameLongRaw(std::ostream& out, const Bits128& frame) {
		uint64_t buffer[3];
		buffer[0] = frame.low();
		buffer[1] = (frame.high() & 0xffffffffffffull);
		buffer[2] = currentTimestamp();
		out.write(reinterpret_cast<char*>(buffer), 24);
	}

	inline void printFrameShortRaw(std::ostream& out, const uint64_t& frameShort) {
		uint64_t buffer[3];
		buffer[0] = (frameShort & 0xffffffffffffffull);
		buffer[1] = 0;
		buffer[2] = currentTimestamp();
		out.write(reinterpret_cast<char*>(buffer), 24);
	}
} // end of namespace