/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *                Enrico Lorenzoni 
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "CRC.hpp"
#include <vector>
#include <array>
#include <iostream>

namespace CRC {

	// the base class for an error correcting ("hash") table of fixed size
	template<size_t size, bool resolveCollisions = false>
	class BaseErrorTable {
	public:
		constexpr BaseErrorTable() : m_keys(), m_ops(), m_occupied() {
			for (size_t i = 0; i < size; i++) {
				m_keys[i] = 0x0;
				m_ops[i] = fixop_t({0,0});
			}
		}
		
		constexpr fixop_t lookup(crc_t crc) const noexcept {
			size_t index = crc % size;
			// The tables live on the bad-message path, which in practice is fed
			// noise syndromes: the overwhelming majority of lookups are misses
			// and have to touch nothing but a miss filter. The home slot of a
			// bucket can only hold an entry the moment something lands in that
			// bucket (a collision chains off the home slot), so a clear bit
			// settles the lookup without reading m_keys at all.
			if (!isOccupied(index))
				return fixop_t({0,0});
			if constexpr (!resolveCollisions) {
				if (m_keys[index] == crc) {
					return m_ops[index];
				}
			} else {
				const auto homeKey = m_keys[index];
				if ((homeKey & ~collisionFlag) == crc) {
					return m_ops[index];
				}
				if ((homeKey & collisionFlag) == 0) {
					return fixop_t({0,0});
				}
				for (size_t probe = 1; probe < size; probe++) {
					index = (index + 1 == size) ? 0 : index + 1;
					if ((m_keys[index] & ~collisionFlag) == crc) {
						return m_ops[index];
					}
					if (m_keys[index] == 0) {
						break;
					}
				}
			}
			return fixop_t({0,0});
		}

	protected:
		constexpr void insert(fixop_t op) noexcept {
			crc_t crc = compute(op);
			size_t index = crc % size;
			if constexpr (!resolveCollisions) {
				if (m_keys[index] == 0) {
					m_keys[index] = crc;
					m_ops[index] = op;
					setOccupied(index);
				}
			} else {
				if (m_keys[index] == 0) {
					m_keys[index] = crc;
					m_ops[index] = op;
					setOccupied(index);
					return;
				}
				if ((m_keys[index] & ~collisionFlag) == crc) {
					m_ops[index] = op;
					return;
				}
				m_keys[index] |= collisionFlag;
				for (size_t probe = 1; probe < size; probe++) {
					index = (index + 1 == size) ? 0 : index + 1;
					if (m_keys[index] == 0) {
						m_keys[index] = crc;
						m_ops[index] = op;
						setOccupied(index);
						return;
					}
					if ((m_keys[index] & ~collisionFlag) == crc) {
						m_ops[index] = op;
						return;
					}
				}
			}
		}
	private:
		// CRC syndromes use 24 bits, leaving the top bit free to mark collision chains.
		static constexpr crc_t collisionFlag = crc_t{1} << 31;
		std::array<crc_t, size> m_keys;	
		std::array<fixop_t, size> m_ops;

		// One bit per slot mirroring "a key lives here", so a miss never has to
		// touch m_keys. The DF17 table is 6790 slots, so this is under a
		// kilobyte and stays in L1.
		static constexpr size_t wordCount = (size + 63) / 64;
		std::array<uint64_t, wordCount> m_occupied;

		constexpr bool isOccupied(size_t index) const noexcept {
			return (m_occupied[index >> 6] >> (index & 63)) & 0x1;
		}

		constexpr void setOccupied(size_t index) noexcept {
			m_occupied[index >> 6] |= (uint64_t(1) << (index & 63));
		}
	};

	// Error correction table used for extended squitter messages
	class DF17ErrorTable : public BaseErrorTable<2343> {
	public:
		constexpr DF17ErrorTable() {
			// one bit error correction excluding the DF part
			for (int i = 0; i < 112-5; i++) {
				insert(encodeFixOp(0x1,i));
			}

			// one-one bit errors
			for (int i = 0; i < 111-5; i++) {
				insert(encodeFixOp(0x3,i));
			}

			// 1 000000 1 pattern shifted through the parity part
			for (int i = 0; i < 16; i++) {
				insert(encodeFixOp(129,i));
			}
		}
	};


	// Error correction table used for extended squitter messages
	class DF17ErrorTableExperimental : public BaseErrorTable<6790, true> {
	public:
		constexpr DF17ErrorTableExperimental() {
			// one bit error correction excluding the DF part
			for (int i = 0; i < 112-5; i++) {
				insert(encodeFixOp(0x1,i));
			}

			// one-one bit errors
			for (int i = 0; i < 111-5; i++) {
				insert(encodeFixOp(0x3,i));
			}

			// 111 bit errors
			for (int i = 0; i < 110-5; i++) {
				insert(encodeFixOp(0x7,i));
			}

			// 1 000000 1 pattern shifted through the parity part
			for (int i = 0; i < 105-5; i++) {
				insert(encodeFixOp(129,i));
			}

			// try inserting 101 bit errors into the table. Not all of them may fit
			for (int i = 0; i < 110-5; i++) {
				insert(encodeFixOp(0x5,i));
			}

			// try two bit errors with two correct bits in between (1001)
			for (int i = 0; i < 109-5; i++) {
				insert(encodeFixOp(0x9,i));
			}

			// try two bit errors with three correct bits in between (10001)
			for (int i = 0; i < 108-5; i++) {
				insert(encodeFixOp(0x11,i));
			}

			// try two bit errors with four correct bits in between (100001)
			for (int i = 0; i < 107-5; i++) {
				insert(encodeFixOp(0x21,i));
			}

			// try two bit errors with five correct bits in between (1000001)
			for (int i = 0; i < 106-5; i++) {
				insert(encodeFixOp(0x41,i));
			}
		}
	};

	// Error correction table for df11 messages
	class DF11ErrorTable : public BaseErrorTable<225> {
	public:
		constexpr DF11ErrorTable() {
			// one bit error correction excluding the DF part
			for (int i = 0; i < 56-5; i++) {
				insert(encodeFixOp(0x1,i));
			}
		}
	};

	// Error correction table for df11 messages experimental
	class DF11ErrorTableExperimental : public BaseErrorTable<469> {
	public:
		constexpr DF11ErrorTableExperimental() {
			// one bit error correction excluding the DF part
			for (int i = 0; i < 56-5; i++) {
				insert(encodeFixOp(0x1,i));
			}

			for (int i = 0; i < 55-5; i++) {
				insert(encodeFixOp(0x3,i));
			}
		}
	};

	// We declare global instances here, but they have to be inline
	// to not show up every time as a separate copy
	inline constexpr DF17ErrorTableExperimental df17ErrorTable;
	
	inline constexpr DF11ErrorTableExperimental df11ErrorTable;

} // end of namespace

