/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <memory>

class ICAOTable {
public:
	static constexpr auto TTL_not_trusted { 5 };
	static constexpr auto TTL_trusted { 30 };
    // number if bits used for the look up table 
	static constexpr auto NumBits { 16 };

    // Length of the table
	static constexpr auto Size{ 0x1 << NumBits };

    // lookup mask
    static constexpr uint32_t HashMask{(0x1 << NumBits) - 1};
  
    // icao address entry
    struct Entry {
        // flag indicating if we trust the address or not (with some padding)
		// this will be removed in the future.
        uint32_t trusted : 5;
        // icao address together with the transponder capabilities
        uint32_t icao : 27;

		// time to live for an untrusted entry
		uint16_t ttl;

		// time to live for the trusted version
		uint16_t ttl_trusted;
    };

    // simple struct keeping an index
	struct Iterator {
        // index in the table
		uint32_t key;

		// default constructor creating a new invalid entry
		constexpr Iterator() : key(Size) { }

		// constructor for setting the key. Assumes that key is a valid key
		constexpr Iterator(uint32_t i) : key(i) { }

		// returns true if this entry is valid
		constexpr bool isValid() const {
			return (key < Size); 
		}
	};

	ICAOTable() {
		m_table = std::make_unique<Entry[]>(Size);
		std::fill(m_table.get(), m_table.get() + Size, Entry{0x0, 0x0, 0, 0});
	}

	void insertWithCA(uint32_t icaoWithCA) {
		const auto key = icaoWithCA & HashMask; 
		m_table[key].icao = icaoWithCA;
		m_table[key].trusted = 0x0;
	}

	Iterator findWithCA(uint32_t icaoWithCA) const {
		const auto key = icaoWithCA & HashMask; 
		return (m_table[key].icao == icaoWithCA) ? Iterator(key) : Iterator();
	}

	Iterator find(uint32_t icao) const {
		const auto key = icao & HashMask; 
		return ((m_table[key].icao & 0xffffffu) == icao) ? Iterator(key) : Iterator();
	}

	void tick() {
		// the counter will wrap around every second exactly once
		m_time1Mhz = (m_time1Mhz + 1) % 1000000;
		
		// if the counter has a value greater than number of entries,
		// we are done here.
		if (m_time1Mhz >= (0x1 << NumBits))
			return;

		// to a tick for the next entry otherwise
		doTickForEntry(m_time1Mhz);
	}

	void markAsTrustedSeen(const Iterator& entry) {
		m_table[entry.key].ttl_trusted = TTL_trusted;
		m_table[entry.key].ttl = TTL_not_trusted;
	}

	void markAsSeen(const Iterator& entry) {
		m_table[entry.key].ttl = TTL_not_trusted;
	}

	bool isTrusted(const Iterator& entry) const {
		return isAlive(entry) && (m_table[entry.key].ttl_trusted > 0);
	}

	bool isAlive(const Iterator& entry) const {
		return m_table[entry.key].ttl > 0;
	}
private:
	void doTickForEntry(uint16_t index) {
		auto& entry = m_table[index];
		if (entry.icao == 0x0)
			return;

		if (entry.ttl_trusted > 0) {
			entry.ttl_trusted--;
		} else {			
			entry.trusted = 0x0;
		}

		if (entry.ttl > 0) {
			entry.ttl--;
		} else {
			entry.trusted = 0x0;
			entry.ttl_trusted = 0;
			entry.icao = 0x0;			
		}
	}

	
	// runs from 0 to 999 999
	uint32_t m_time1Mhz { 0 };

    // the table with the icao addresses including transponder CA 
	std::unique_ptr<Entry[]> m_table;
};
