/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <cstdint>
#include <bitset>

class Bits128 {
public:
	explicit constexpr Bits128() : m_bits{ 0x0,0x0 } {}
	explicit constexpr Bits128(const uint64_t& h, const uint64_t& l) : m_bits{ l, h } {}
	explicit constexpr Bits128(const uint32_t& other) : m_bits{ other, 0x0 } {}
	explicit constexpr Bits128(const uint64_t& other) : m_bits{ other, 0x0 } {}

	constexpr const uint64_t& high() const { return m_bits[1]; }
	constexpr const uint64_t& low() const { return m_bits[0]; }

	constexpr uint64_t& high() { return m_bits[1]; }
	constexpr uint64_t& low() { return m_bits[0]; }

	constexpr void shiftLeft() {
		m_bits[1] = (m_bits[1] << 1) | (m_bits[0] >> 63);
		m_bits[0] <<= 1;
	}

	constexpr void shiftRight() {
		m_bits[0] = (m_bits[0] >> 1) | (m_bits[1] << 63);
		m_bits[1] >>= 1;
	}

	constexpr void shiftLeft(int i) {
		if (i < 64) {
			m_bits[1] = (m_bits[1] << i) | (m_bits[0] >> (64 - i));
			m_bits[0] <<= i;
		}
		else {
			m_bits[1] = m_bits[0] << (i - 64);
			m_bits[0] = 0;
		}
	}

	constexpr void shiftRight(int i) {
		if (i < 64) {
			m_bits[0] = (m_bits[0] >> i) | (m_bits[1] << (64 - i));
			m_bits[1] >>= i;
		}
		else {
			m_bits[0] = m_bits[1] >> (i - 64);
			m_bits[1] = 0;
		}
	}

	constexpr inline void set(uint8_t i, bool b) {
		if (b)
			m_bits[i >> 6] |= (0x1ull << (i & 0x3F));
		else
			m_bits[i >> 6] &= ~(0x1ull << (i & 0x3F));
	}

	constexpr inline void flip(uint8_t i) {
		m_bits[i >> 6] ^= (0x1ull << (i & 0x3F));
	}

	constexpr bool get(uint8_t i) const {
		return (m_bits[i >> 6] & (0x1ull << (i & 0x3F))) != 0;
	}

	constexpr bool operator[](uint8_t i) const {
		return get(i);
	}

	constexpr Bits128& operator &= (const Bits128& other) {
		m_bits[0] &= other.m_bits[0];
		m_bits[1] &= other.m_bits[1];
		return *this;
	}

	constexpr Bits128& operator &= (const uint64_t& other) {
		m_bits[0] &= other;
		m_bits[1] = 0;
		return *this;
	}

	constexpr Bits128& operator |= (const Bits128& other) {
		m_bits[0] |= other.m_bits[0];
		m_bits[1] |= other.m_bits[1];
		return *this;
	}

	constexpr Bits128& operator |= (const uint64_t& other) {
		m_bits[0] |= other;
		return *this;
	}

	constexpr Bits128& operator ^= (const Bits128& other) {
		m_bits[0] ^= other.m_bits[0];
		m_bits[1] ^= other.m_bits[1];
		return *this;
	}

	constexpr Bits128& operator ^= (const uint64_t& other) {
		m_bits[0] ^= other;
		return *this;
	}

	constexpr Bits128& operator <<= (uint8_t i) {
		shiftLeft(i);
		return *this;
	}

	constexpr Bits128& operator >>= (uint8_t i) {
		shiftRight(i);
		return *this;
	}

	constexpr bool operator == (const Bits128& other) const {
		return ((m_bits[0] == other.m_bits[0]) && (m_bits[1] == other.m_bits[1]));
	}

	constexpr Bits128& operator = (uint64_t other) {
		m_bits[0] = other;
		m_bits[1] = 0;
		return *this;
	}

	template<typename T>
	constexpr Bits128 operator & (const T& other) {
		Bits128 res(*this);
		return res &= other;
	}

	template<typename T>
	constexpr Bits128 operator | (const T& other) {
		Bits128 res(*this);
		return res |= other;
	}

	template<typename T>
	constexpr Bits128 operator ^ (const T& other) {
		Bits128 res(*this);
		return res ^= other;
	}

	template<std::size_t numBits>
	constexpr void copyToBitset(std::bitset<numBits>& bits) const {
		for (auto i = 0; i < numBits; i++) {
			bits[i] = get(i);
		}
	}
private:
	// two 64 bits.
	uint64_t m_bits[2];
};