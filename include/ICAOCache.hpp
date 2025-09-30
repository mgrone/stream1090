/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <memory>

class CacheWithTimeStamp {
public:
	static constexpr auto NumBits { 16 };
	static constexpr auto Size{ 0x1 << NumBits };
	static constexpr uint32_t HashMask{(0x1 << NumBits) - 1};

	struct Entry {
		
		uint32_t key;
		// default constructor creating a new invalid entry
		Entry() : key(Size) { }

		// constructor for setting the key. Assumes that key is a valid key
		Entry(uint32_t i) : key(i) { }

		// returns true if this entry is valid
		constexpr bool isValid() const {
			return (key < Size); 
		}
	};

	CacheWithTimeStamp() {
		m_table = std::make_unique<uint32_t[]>(Size);
		m_time  = std::make_unique<uint64_t[]>(Size);
		std::fill(m_table.get(), m_table.get() + Size, 0xffffffff);
		std::fill(m_time.get(), m_time.get() + Size, 0);
	}

	void upsertWithCA(uint32_t icaoWithCA, uint64_t currTime) {
		const auto key = icaoWithCA & HashMask; 
		m_table[key] = icaoWithCA;
		m_time[key] = currTime;
	}

	void markAsSeen(const Entry& entry, uint64_t currTime) {
		m_time[entry.key] = currTime;
	}

	Entry findWithCA(uint32_t icaoWithCA) const {
		const auto key = icaoWithCA & HashMask; 
		return (m_table[key] == icaoWithCA) ? Entry(key) : Entry();
	}

	Entry find(uint32_t icao) const {
		const auto key = icao & HashMask; 
		return ((m_table[key] & 0xffffff) == icao) ? Entry(key) : Entry();
	}

	uint64_t lastSeen(const Entry& entry) const {
		return (m_time[entry.key]); 
	}

	bool validAndNotOlderThan(const Entry& entry, uint64_t currTime, uint64_t timeout) const {
		return (entry.isValid() && ((currTime - lastSeen(entry)) < timeout));
	}

private:
	std::unique_ptr<uint32_t[]> m_table;
	std::unique_ptr<uint64_t[]> m_time;
};