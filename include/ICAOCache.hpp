/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <array>
#include <memory>

class ICAOTable {
public:
	static constexpr auto TTL_not_trusted { 10 };
	static constexpr auto TTL_trusted { 30 };
	// tick() runs at 1 MHz. A DF11 frame lasts 56 us, so 100 us excludes
	// detections of the same transmission while two seconds keeps the pair local.
	static constexpr uint32_t DF11CandidateMinTicks { 100 };
	static constexpr uint32_t DF11CandidateMaxTicks { 2'000'000 };
	//static constexpr auto ALT_delta_25ft { 80 };
	static constexpr auto ALT_delta_ft { 2000 };
	// Number of bits used for the lookup set and entries kept per set.
	static constexpr auto NumBits { 16 };
	static constexpr auto NumWays { 2 };
	static constexpr auto NumSets { 0x1 << NumBits };
	static constexpr auto Size { NumSets * NumWays };

    // lookup mask
    static constexpr uint32_t HashMask{(0x1 << NumBits) - 1};
  
    // icao address entry
    struct Entry {
        // icao address together with the transponder capabilities
        uint32_t icao;

		// time to live for an untrusted entry
		uint16_t ttl;

		// time to live for the trusted version
		uint16_t ttl_trusted;
    };

	struct MsgStatEntry {
		// timestamp of the last message that was either emitted or was a dupe
		uint64_t last_time;
	};

	struct SquawkAlt {
		// the last sqauwk code received
		uint16_t squawk_cnt : 3;
		uint16_t squawk : 13;

		// the last altitude in feet received
		uint8_t altitude_cnt;
		uint16_t altitude;
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
		std::fill(m_table.get(), m_table.get() + Size, Entry{0x0, 0, 0});

		m_squawkAlt = std::make_unique<SquawkAlt[]>(Size);
		std::fill(m_squawkAlt.get(), m_squawkAlt.get() + Size, SquawkAlt{0, 0, 0, 0});

		m_msgStatTable = std::make_unique<MsgStatEntry[]>(Size);
		std::fill(m_msgStatTable.get(), m_msgStatTable.get() + Size, MsgStatEntry{0});
	}

	Iterator insertWithCA(uint32_t icaoWithCA) noexcept  {
		const auto base = (icaoWithCA & HashMask) * NumWays;
		uint32_t key = base;
		for (uint32_t way = 0; way < NumWays; ++way) {
			const auto candidate = base + way;
			if (m_table[candidate].icao == 0) {
				key = candidate;
				break;
			}
			if (m_table[candidate].ttl_trusted < m_table[key].ttl_trusted
					|| (m_table[candidate].ttl_trusted == m_table[key].ttl_trusted
						&& m_table[candidate].ttl < m_table[key].ttl)) {
				key = candidate;
			}
		}
		doResetEntry(key);
		m_table[key].icao = icaoWithCA;
		return Iterator(key);
	}

	Iterator findWithCA(uint32_t icaoWithCA) const noexcept {
		const auto base = (icaoWithCA & HashMask) * NumWays;
		for (uint32_t way = 0; way < NumWays; ++way) {
			const auto key = base + way;
			if (m_table[key].icao == icaoWithCA)
				return Iterator(key);
		}
		return Iterator();
	}

	Iterator find(uint32_t icao) const noexcept {
		const auto base = (icao & HashMask) * NumWays;
		for (uint32_t way = 0; way < NumWays; ++way) {
			const auto key = base + way;
			if ((m_table[key].icao & 0xffffffu) == icao)
				return Iterator(key);
		}
		return Iterator();
	}

	bool confirmDF11Candidate(uint32_t icaoWithCA) noexcept {
		auto& candidate = m_df11Candidates[df11CandidateIndex(icaoWithCA)];
		const auto age = m_df11Clock - candidate.firstSeen;

		if (candidate.icaoWithCA != 0 && candidate.icaoWithCA == icaoWithCA) {
			if (age >= DF11CandidateMinTicks && age <= DF11CandidateMaxTicks) {
				candidate = DF11Candidate{};
				return true;
			}
			if (age < DF11CandidateMinTicks)
				return false;
		}

		candidate = DF11Candidate{icaoWithCA, m_df11Clock};
		return false;
	}

	void tick() noexcept {
		m_df11Clock++;
		
		// the counter will wrap around every second exactly once.
		// A compare beats the modulo here: this runs once per microsecond and
		// the branch is taken once per second.
		if (++m_time1Mhz == 1000000)
			m_time1Mhz = 0;

		// if the counter has a value greater than number of entries,
		// we are done here.
		if (m_time1Mhz >= NumSets)
			return;

		// Tick every way in the selected set so each entry ages once per second.
		const auto base = m_time1Mhz * NumWays;
		for (uint32_t way = 0; way < NumWays; ++way)
			doTickForEntry(base + way);
	}

	/// Membership test against a 16 KB bitmap mirroring "this slot currently
	/// holds a trusted entry". The table itself is 131072 entries of 8 bytes, so
	/// a probe is an L2/DRAM access; callers that test addresses at demodulation
	/// rate need a filter that stays in L1.
	///
	/// Conservative by construction: markAsTrustedSeen() sets the bit
	/// immediately, so it is never clear for a trusted entry. A stale set bit is
	/// harmless because callers still confirm with findWithCA(), and the tick
	/// sweep clears it within one pass over the table.
	bool maybeTrusted(uint32_t icaoWithCA) const noexcept {
		const auto base = (icaoWithCA & HashMask) * NumWays;
		for (uint32_t way = 0; way < NumWays; ++way) {
			const auto key = base + way;
			if ((m_trustedBits[key >> 6] >> (key & 63)) & 0x1)
				return true;
		}
		return false;
	}

	void markAsTrustedSeen(const Iterator& entry) noexcept {
		m_table[entry.key].ttl_trusted = TTL_trusted;
		m_table[entry.key].ttl = TTL_not_trusted;
		setTrustedBit(entry.key);
	}

	void markAsSeen(const Iterator& entry, uint16_t ttl = TTL_not_trusted) noexcept {
		m_table[entry.key].ttl = ttl;
	}

	bool isTrusted(const Iterator& entry) const noexcept {
		return isAlive(entry) && (m_table[entry.key].ttl_trusted > 0);
	}

	bool isAlive(const Iterator& entry) const noexcept {
		return m_table[entry.key].ttl > 0;
	}

	bool checkSquawk(const Iterator& entry, uint16_t newSquawk) noexcept {
		if (newSquawk == 0) {
			return false;
		}

		if (m_squawkAlt[entry.key].squawk == newSquawk) {
			m_squawkAlt[entry.key].squawk_cnt = 1;
			return true;
		}

		if (m_squawkAlt[entry.key].squawk_cnt == 0) {
			m_squawkAlt[entry.key].squawk = newSquawk;
		} else {
			m_squawkAlt[entry.key].squawk_cnt = 0;
		}
		return false;
	}

	bool checkAltitude(const Iterator& entry, uint16_t newAlt) noexcept {
		if (newAlt == 0) {
			return false;
		}

		const auto delta = abs((int)m_squawkAlt[entry.key].altitude - (int)newAlt);
		if ((delta <= ALT_delta_ft)) {
			m_squawkAlt[entry.key].altitude = newAlt;
			m_squawkAlt[entry.key].altitude_cnt = 1;
			return true;
		};

		if (m_squawkAlt[entry.key].altitude_cnt == 0) {
			m_squawkAlt[entry.key].altitude = newAlt;
		} else {
			m_squawkAlt[entry.key].altitude_cnt = 0;
		}
		return false;
	}

	MsgStatEntry& getMsgStatEntry(const Iterator& it) noexcept {
		return m_msgStatTable[it.key];
	}
private:
	struct DF11Candidate {
		uint32_t icaoWithCA { 0 };
		uint64_t firstSeen { 0 };
	};

	static constexpr size_t df11CandidateIndex(uint32_t icaoWithCA) noexcept {
		return (icaoWithCA * 0x9e3779b1u) >> 24;
	}

	void setTrustedBit(uint32_t key) noexcept {
		m_trustedBits[key >> 6] |= (uint64_t(1) << (key & 63));
	}

	void clearTrustedBit(uint32_t key) noexcept {
		m_trustedBits[key >> 6] &= ~(uint64_t(1) << (key & 63));
	}

	std::array<uint64_t, Size / 64> m_trustedBits{};

	void doTickForEntry(uint32_t index) noexcept {
		auto& entry = m_table[index];
		if (entry.icao == 0x0)
			return;

		if (entry.ttl_trusted > 0) {
			entry.ttl_trusted--;
		}

		if (entry.ttl > 0) {
			entry.ttl--;
		} else {
			doResetEntry(index);	
		}

		// keep the trusted-membership filter in step with the entry
		if (m_table[index].ttl > 0 && m_table[index].ttl_trusted > 0)
			setTrustedBit(index);
		else
			clearTrustedBit(index);
	}

	void doResetEntry(uint32_t index) noexcept {
		auto& entry = m_table[index];
		entry.icao = 0x0;
		entry.ttl_trusted = 0;
		entry.ttl = 0;
		m_msgStatTable[index].last_time = 0;
		m_squawkAlt[index] = SquawkAlt{0, 0, 0, 0};
	}

	
	// runs from 0 to 999 999
	uint32_t m_time1Mhz { 0 };
	uint64_t m_df11Clock { 0 };
	std::array<DF11Candidate, 256> m_df11Candidates{};

    // the table with the icao addresses including transponder CA 
	std::unique_ptr<Entry[]> m_table;

	// the table for the squawk and altitude data  
	std::unique_ptr<SquawkAlt[]> m_squawkAlt;

	// the table with the msg timestamps
	std::unique_ptr<MsgStatEntry[]> m_msgStatTable;
};
