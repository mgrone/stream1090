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
#include <optional>

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

	constexpr inline uint8_t extractSurvInterrogator_Short(const uint64_t& frameShort) {
		return ((frameShort >> 39) & 0xf);
	}

	constexpr inline uint16_t decodeSquawk(uint16_t bits) {
		uint16_t a = ((bits >> 11) & 1) | (((bits >> 9) & 1) << 1) | (((bits >> 7) & 1) << 2);
		uint16_t c = ((bits >> 12) & 1) | (((bits >> 10) & 1) << 1) | (((bits >> 8) & 1) << 2);

		uint16_t b = ((bits >> 5) & 1) | (((bits >> 3) & 1) << 1) | (((bits >> 1) & 1) << 2);
		uint16_t d = ((bits >> 4) & 1) | (((bits >> 2) & 1) << 1) | (((bits     ) & 1) << 2);
	
		return a * 1000 + b * 100 + c * 10 + d;
	}

	constexpr inline int32_t decodeAltitudeBitsFeet(uint16_t bits) {
		const uint16_t a = (bits & 0xf); // lower 4 bits
		const uint16_t b = (bits >> 5) & 0x1; // 6th bit
		const uint16_t c = (bits >> 7) & 0x3f; // higher 6 bits
		return (a | (b << 4) | (c << 5)) * 25 - 1000;
	}

	constexpr inline uint16_t decodeGillhamID13(uint16_t bits) {
		uint16_t modeA = 0;
		if (bits & 0x1000) modeA |= 0x0010; // C1
		if (bits & 0x0800) modeA |= 0x1000; // A1
		if (bits & 0x0400) modeA |= 0x0020; // C2
		if (bits & 0x0200) modeA |= 0x2000; // A2
		if (bits & 0x0100) modeA |= 0x0040; // C4
		if (bits & 0x0080) modeA |= 0x4000; // A4
		if (bits & 0x0020) modeA |= 0x0100; // B1
		if (bits & 0x0010) modeA |= 0x0001; // D1 / Q
		if (bits & 0x0008) modeA |= 0x0200; // B2
		if (bits & 0x0004) modeA |= 0x0002; // D2
		if (bits & 0x0002) modeA |= 0x0400; // B4
		if (bits & 0x0001) modeA |= 0x0004; // D4
		return modeA;
	}

	constexpr inline std::optional<int32_t> decodeGillhamAltitude(uint16_t bits) {
		const uint16_t modeA = decodeGillhamID13(bits);
		if ((modeA & 0x8889) != 0 || (modeA & 0x00f0) == 0)
			return std::nullopt;

		uint16_t oneHundreds = 0;
		uint16_t fiveHundreds = 0;
		if (modeA & 0x0010) oneHundreds ^= 0x007;
		if (modeA & 0x0020) oneHundreds ^= 0x003;
		if (modeA & 0x0040) oneHundreds ^= 0x001;
		if ((oneHundreds & 5) == 5) oneHundreds ^= 2;
		if (oneHundreds > 5)
			return std::nullopt;

		if (modeA & 0x0002) fiveHundreds ^= 0x0ff;
		if (modeA & 0x0004) fiveHundreds ^= 0x07f;
		if (modeA & 0x1000) fiveHundreds ^= 0x03f;
		if (modeA & 0x2000) fiveHundreds ^= 0x01f;
		if (modeA & 0x4000) fiveHundreds ^= 0x00f;
		if (modeA & 0x0100) fiveHundreds ^= 0x007;
		if (modeA & 0x0200) fiveHundreds ^= 0x003;
		if (modeA & 0x0400) fiveHundreds ^= 0x001;
		if (fiveHundreds & 1) oneHundreds = 6 - oneHundreds;

		const int32_t hundreds = fiveHundreds * 5 + oneHundreds - 13;
		if (hundreds < -12)
			return std::nullopt;
		return hundreds * 100;
	}

	constexpr inline std::optional<int32_t> decodeAltitude(uint16_t bits) {
		if (bits & 0x0040) // M=1: metric encoding is not implemented.
			return std::nullopt;

		if (bits & 0x0010)
			return decodeAltitudeBitsFeet(bits);
		return decodeGillhamAltitude(bits);
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

namespace MLAT {
	template<int NumStreams>
	constexpr uint64_t sampleIndexToMlatTime(uint64_t sampleTime) {
		constexpr double ratio = 12.0/(double)NumStreams;
		return (uint64_t)(sampleTime * ratio);
	}

	template<>
	constexpr uint64_t sampleIndexToMlatTime<8>(uint64_t sampleTime) {
		// for 8 Mhz we have 1.5 * 8 = 12
		return sampleTime + (sampleTime >> 1);
	}

	template<>
	constexpr uint64_t sampleIndexToMlatTime<16>(uint64_t sampleTime) {
		// for 16 Mhz we have 0.75 * 16 = (0.5 + 0.25) * 16 = 12
		return (sampleTime >> 1) + (sampleTime >> 2);
	}

	template<>
	constexpr uint64_t sampleIndexToMlatTime<6>(uint64_t sampleTime) {
		// for 6 Mhz we have 2.0 * 6 = 12
		return (sampleTime << 1);
	}

	template<>
	constexpr uint64_t sampleIndexToMlatTime<12>(uint64_t sampleTime) {
		// for 12 Mhz nothing to do
		return sampleTime;
	}

	template<>
	constexpr uint64_t sampleIndexToMlatTime<24>(uint64_t sampleTime) {
		// for 24 Mhz we simply divide by 2
		return (sampleTime >> 1);
	}

	template<>
	constexpr uint64_t sampleIndexToMlatTime<48>(uint64_t sampleTime) {
		// for 48 Mhz we simply divide by 4
		return (sampleTime >> 2);
	}

	template<>
	constexpr uint64_t sampleIndexToMlatTime<10>(uint64_t sampleTime) {
		// for 10 Mhz we have 12/10 = 6/5 = 1 + 1/5
		return sampleTime + sampleTime/5;
	}

	template<>
	constexpr uint64_t sampleIndexToMlatTime<20>(uint64_t sampleTime) {
		// for 20 Mhz we have 12/20 = 6/10 = 1/2 + 1/10 
		return (sampleTime >> 1) + sampleTime/10;
	}

	template<>
	constexpr uint64_t sampleIndexToMlatTime<40>(uint64_t sampleTime) {
		// for 40 Mhz we have 12/40 = 3/10 = 1/4 + 1/20 
		return (sampleTime >> 2) + sampleTime/20;
	}
} // end of namespace MLAT

