/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "Bits128.hpp"
#include <array>

namespace CRC {

	// the type used for the 24-bit CRC 
	typedef uint32_t crc_t;

	// struct representing an operation that xor's pattern shifted by index
	struct fixop_t {
		uint8_t pattern;
		uint8_t index;

		constexpr bool valid() const {
			return pattern != 0;
		}
	};

	// the magic 25-bit polynomial used here for the computation of crcs
	constexpr crc_t polynomial = 0x1FFF409;
	
	// function to shift in a bit at the right 
	constexpr void pushBack(crc_t& crc, bool bit) {
		crc = (crc << 1) | (crc_t)bit;
		if (crc & (0x1 << 24))
			crc ^= polynomial;
	}

	template<uint8_t numBits>
	constexpr crc_t compute(const Bits128& bits) {
		crc_t result = 0;
		for (int i = numBits-1; i >= 0; i--) {
			pushBack(result, bits[i]);
		}
		return result;
	}

	// the delta to zero out a 1 bit in the crc 
	template<uint8_t bitIndex>
	constexpr crc_t delta() {
		crc_t crc = 0x1;
		for (uint8_t i = 0; i < bitIndex; i++) {
			crc = (crc << 1);
			if (crc & (0x1 << 24))
				crc ^= polynomial;
		}
		return crc;
	}

	// precomputed value for 112th bit
	template<>
	constexpr crc_t delta<111>() {
		return 0x3935EA;
	}

	// precomputed value for 56th bit
	template<>
	constexpr crc_t delta<55>() {
		return 0x18567;
	}

	// for compat with older code
	constexpr fixop_t encodeFixOp(uint8_t pattern, uint8_t index) {
		return fixop_t({pattern, index});
	}

	// applies the operation op to bits by flipping the bits accordingly
	constexpr void applyFixOp(fixop_t op, Bits128& bits, uint8_t offset = 0) {
		Bits128 bitsToFlip((uint64_t)op.pattern);
		bitsToFlip.shiftLeft(op.index + offset);
		bits = bits ^ bitsToFlip;
	}

	// applies the operation op to bits by flipping the bits accordingly
	constexpr void applyFixOp(fixop_t op, uint64_t& frameShort, uint8_t offset = 0) {
		uint64_t bitsToFlip = ((uint64_t)op.pattern) << (op.index + offset);
		frameShort = frameShort ^ bitsToFlip;
	}

	// overloaded function to compute the crc of the pattern of an operation op.
	// This works, because the unshifted pattern of op is less than 25 bits and
	// we are only performing a left shift here
	constexpr crc_t compute(fixop_t op) {
		crc_t crc = op.pattern;
		for (auto i = 0; i < op.index; i++) {
			crc = (crc << 1);
			// if we shift out a 1, subtract the polynomial
			if (crc & (0x1 << 24))
				crc ^= polynomial;
		}
		return crc;
	}

} // end of namespace