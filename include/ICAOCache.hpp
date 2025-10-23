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
    // number if bits used for the look up table 
	static constexpr auto NumBits { 16 };

    // Length of the table
	static constexpr auto Size{ 0x1 << NumBits };

    // lookup mask
    static constexpr uint32_t HashMask{(0x1 << NumBits) - 1};
  
    // icao address entry
    struct Entry {
        // flag indicating if we trust the address or not (with some padding)
        uint32_t trusted : 5;
        // icao address together with the transponder capabilities
        uint32_t icao : 27;
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
		m_time  = std::make_unique<uint64_t[]>(Size);
		std::fill(m_table.get(), m_table.get() + Size, Entry{0x0, 0x7ffffff});
		std::fill(m_time.get(), m_time.get() + Size, 0);
	}

	void insertWithCA(uint32_t icaoWithCA, uint64_t currTime) {
		const auto key = icaoWithCA & HashMask; 
		m_table[key].icao = icaoWithCA;
		m_table[key].trusted = 0x0;
		m_time[key] = currTime;
	}

    void markAsTrusted(const Iterator& entry) const {
        m_table[entry.key].trusted = 0x1;
    }

	void markAsSeen(const Iterator& entry, uint64_t currTime) {
		m_time[entry.key] = currTime;
	}

	Iterator findWithCA(uint32_t icaoWithCA) const {
		const auto key = icaoWithCA & HashMask; 
		return (m_table[key].icao == icaoWithCA) ? Iterator(key) : Iterator();
	}

	Iterator find(uint32_t icao) const {
		const auto key = icao & HashMask; 
		return ((m_table[key].icao & 0xffffffu) == icao) ? Iterator(key) : Iterator();
	}

	uint64_t lastSeen(const Iterator& entry) const {
		return (m_time[entry.key]); 
	}

    bool isTrusted(const Iterator& entry) const {
        return (m_table[entry.key].trusted);
    }

	bool notOlderThan(const Iterator& entry, uint64_t currTime, uint64_t timeout) const {
		return ((currTime - lastSeen(entry)) < timeout);
	}

private:
    // the table with the icao addresses 
	std::unique_ptr<Entry[]> m_table;

    // a separate table for the last seen time in sample time
    // this is 64 bit because for high sample speeds 32 bit will wrap around quite fast.
	std::unique_ptr<uint64_t[]> m_time;
};
