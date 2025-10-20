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
	// compares the 56 lowest bits of a and b
	constexpr inline bool equalShort(const Bits128& a, const Bits128& b) {
		return ((a.low() << 8) == (b.low() << 8));
	}

	// compares the 112 lowest bits of a and b
	constexpr inline bool equalLong(const Bits128& a, const Bits128& b) {
		/*if (a.low() != b.low())
			return false;

		return ((a.high() ^ b.high()) & 0xffffffffffffull) == 0; */
		return (a.low() == b.low()) && ((a.high() << 16) == (b.high() << 16));
	}

	constexpr inline uint8_t extractDownlinkFormat112(const Bits128& frame) {
		return (frame.high() >> 43) & 0b11111ull;
	}

	constexpr inline uint8_t extractDownlinkFormat56(const Bits128& frame) {
		return (frame.low() >> 51) & 0b11111ull;
	}

	constexpr inline uint8_t DF17_extractCapability(const Bits128& frame) {
		return (frame.high() >> 40) & (0b111ull);
	}

	constexpr inline uint32_t DF17_extractICAO(const Bits128& frame) {
		return ((frame.high() >> 16) & (0xffffffull));
	}

	constexpr inline uint32_t DF17_extractICAOWithCA(const Bits128& frame) {
		return ((frame.high() >> 16) & (0x7ffffffull));
	}

	constexpr inline uint8_t DF17_extractTypeCode(const Bits128& frame) {
		return ((frame.high() >> 11) & (0b11111ull));
	}

	constexpr inline uint32_t DF11_extractICAO(const Bits128& frame) {
		return ((frame.low() >> 24) & 0xffffffull);
	}

	constexpr inline uint32_t DF11_extractICAOWithCA(const Bits128& frame) {
		return ((frame.low() >> 24) & 0x7ffffffull);
	}

	constexpr inline int DF11_extractCapability(const Bits128& frame) {
		return ((frame.low() >> 48) & 0x7ull);
	}

	inline void printFrameLong(std::ostream& out, const Bits128& frame) {
		out << '*' << std::setfill('0') << std::setw(12) << std::hex << (frame.high() & 0xffffffffffffull) << std::setw(16) << frame.low() << ";" << std::endl;
	}

	inline void printFrameShort(std::ostream& out, const Bits128& frame) {
		out << '*' << std::setfill('0') << std::setw(14) << std::hex << (frame.low() & 0xffffffffffffffull) << ";" << std::endl;
	}

	inline void printFrameLongMlat(std::ostream& out, uint64_t timeStamp, const Bits128& frame) {
		// timestamp + frame = @ + 48 bit timestamp + 48 + 64 = 112 bit frame
		out << '@' << std::hex << std::setfill('0') << std::setw(12) << (timeStamp & 0xffffffffffffull) << (frame.high() & 0xffffffffffffull) << std::setw(16) << frame.low() << ";" << std::endl;
	}

	inline void printFrameShortMlat(std::ostream& out, uint64_t timeStamp, const Bits128& frame) {
		// timestamp + frame = @ + 48 bit timestamp + 56 bit frame
		out << '@' << std::hex << std::setfill('0') << std::setw(12) << (timeStamp & 0xffffffffffffull) << std::setw(14) << (frame.low() & 0xffffffffffffffull) << ";" << std::endl;
	}

	inline void DF11_printDebug(std::ostream& out, const Bits128& frame) {
		out << "[DF11:] " << DF11_extractCapability(frame) << " " << std::hex << std::setfill('0') << std::setw(6) << DF11_extractICAO(frame) << std::dec << std::endl; 
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

	inline void printFrameShortRaw(std::ostream& out, const Bits128& frame) {
		uint64_t buffer[3];
		buffer[0] = (frame.low() & 0xffffffffffffffull);
		buffer[1] = 0;
		buffer[2] = currentTimestamp();
		out.write(reinterpret_cast<char*>(buffer), 24);
	}

} // end of namespace