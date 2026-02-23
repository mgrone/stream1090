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

	constexpr inline uint16_t extractSquawkAlt_Long(const Bits128& frameLong) {
		return ((frameLong.high() >> 16) & 0x1fff);
	}

	constexpr inline uint16_t extractSquawkAlt_Short(const uint64_t& frameShort) {
		return ((frameShort >> 24) & 0x1fff);
	}

	constexpr inline uint16_t decodeSquawk(uint16_t bits) {
		uint16_t a = ((bits >> 11) & 1) | (((bits >> 9) & 1) << 1) | (((bits >> 7) & 1) << 2);
		uint16_t c = ((bits >> 12) & 1) | (((bits >> 10) & 1) << 1) | (((bits >> 8) & 1) << 2);

		uint16_t b = ((bits >> 5) & 1) | (((bits >> 3) & 1) << 1) | (((bits >> 1) & 1) << 2);
		uint16_t d = ((bits >> 4) & 1) | (((bits >> 2) & 1) << 1) | (((bits     ) & 1) << 2);
	
		return a * 1000 + b * 100 + c * 10 + d;
	}

	constexpr inline uint16_t decodeAltitudeBitsFeet(uint16_t bits) {
		const uint16_t a = (bits & 0xf); // lower 4 bits
		const uint16_t b = (bits >> 5) & 0x1; // 6th bit
		const uint16_t c = (bits >> 7) & 0x3f; // higher 6 bits
		return (a | (b << 4) | (c << 5)) * 25 - 1000;
	}

	constexpr inline uint16_t decodeAltitudeBitsMeter(uint16_t bits) {
		const uint16_t a = (bits & 0x3f); // lower 6 bits
		const uint16_t b = (bits >> 7) & 0x3f; // higher 6 bits
		return uint16_t(float(a | (b << 6))*3.28084f);
	}

	constexpr inline uint16_t decodeAltitude(uint16_t bits) {
		// TODO: test this 
		//check if the M bit is set and we have to deal with meters
		/*if (bits & (0x1 << 6)) {
			return decodeAltitudeBitsMeter(bits);
		} else if (bits & (0x1 << 4)) {
			return decodeAltitudeBitsFeet(bits);
		}*/

		if (!(bits & (0x1 << 6)) && (bits & (0x1 << 4)))
			return decodeAltitudeBitsFeet(bits);
		
		return 0;
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
		out.flush();
	}

	inline void printFrameShortRaw(std::ostream& out, const uint64_t& frameShort) {
		uint64_t buffer[3];
		buffer[0] = (frameShort & 0xffffffffffffffull);
		buffer[1] = 0;
		buffer[2] = currentTimestamp();
		out.write(reinterpret_cast<char*>(buffer), 24);
		out.flush();
	}
} // end of namespace