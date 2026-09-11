/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <array>
#include <cstdlib>
#include <limits>
#include <memory>

#include "ModeS.hpp"

class ICAOTable {
public:
	static constexpr auto TTL_not_trusted { 10 };
	static constexpr auto TTL_trusted { 30 };
	// tick() runs at 1 MHz. A DF11 frame lasts 56 us, so 100 us excludes
	// detections of the same transmission while two seconds keeps the pair local.
	static constexpr uint32_t DF11CandidateMinTicks { 100 };
	static constexpr uint32_t DF11CandidateMaxTicks { 2'000'000 };
	// Apply the same separation limits when corroborating rejected AP frames.
	static constexpr uint32_t RejectedCandidateMinTicks { 100 };
	static constexpr uint32_t RejectedCandidateMaxTicks { 2'000'000 };
	//static constexpr auto ALT_delta_25ft { 80 };
	static constexpr auto ALT_delta_ft { 2000 };
	static constexpr auto AltitudeUnset { std::numeric_limits<int16_t>::min() };
    // number if bits used for the look up table 
	static constexpr auto NumBits { 16 };

    // Length of the table
	static constexpr auto Size{ 0x1 << NumBits };

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
		int16_t altitude_25ft;
	};

	struct PositionState {
		uint32_t cpr_even_lat { 0 };
		uint32_t cpr_even_lon { 0 };
		uint32_t cpr_odd_lat { 0 };
		uint32_t cpr_odd_lon { 0 };
		uint64_t cpr_even_time { 0 };
		uint64_t cpr_odd_time { 0 };
		uint32_t output_even_lat { 0 };
		uint32_t output_even_lon { 0 };
		uint32_t output_odd_lat { 0 };
		uint32_t output_odd_lon { 0 };
		uint64_t output_even_time { 0 };
		uint64_t output_odd_time { 0 };
		int32_t last_lat_e5 { 0 };
		int32_t last_lon_e5 { 0 };
		uint64_t position_time { 0 };
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
		std::fill(m_squawkAlt.get(), m_squawkAlt.get() + Size,
			SquawkAlt{0, 0, 0, AltitudeUnset});

		m_positionState = std::make_unique<PositionState[]>(Size);

		m_msgStatTable = std::make_unique<MsgStatEntry[]>(Size);
		std::fill(m_msgStatTable.get(), m_msgStatTable.get() + Size, MsgStatEntry{0});
	}

	Iterator insertWithCA(uint32_t icaoWithCA) noexcept  {
		const auto key = icaoWithCA & HashMask;
		const auto previous = m_table[key].icao;
		if (previous != 0 && (previous & 0xffffffu) == (icaoWithCA & 0xffffffu)) {
			// CA is a per-frame capability field, not part of the aircraft's
			// identity. Refresh it without discarding the trusted aircraft state.
			m_table[key].icao = icaoWithCA;
			return Iterator(key);
		}
		doResetEntry(key);
		m_table[key].icao = icaoWithCA;
		if (icaoWithCA != 0x0)
			setOccupiedBit(key);
		return Iterator(key);
	}

	/*
	 * Both lookups below are on the demodulation hot path: every DF 0, 4, 5,
	 * 16, 20 and 21 candidate reaches one of them, and on those the address is
	 * recovered from the parity, so on noise the probe is a random index into
	 * 65536 entries of 8 bytes. That table plus its two siblings is about
	 * 1.4 MB against 1 MB of L2, so the probe usually goes to DRAM, and with a
	 * few hundred live aircraft in 65536 slots it almost always says "no".
	 *
	 * CPR history lives in a separate cold table so the hot trio stays at that
	 * size. m_occupiedBits mirrors "this slot holds an entry" in 8 KB, which
	 * stays in L1. An empty slot has icao == 0, so a clear bit already settles the
	 * comparison: it matches only a zero query. That makes this an exact
	 * short circuit rather than a heuristic, and the table is never touched on
	 * the rejecting path.
	 */
	Iterator findWithCA(uint32_t icaoWithCA) const noexcept {
		const auto key = icaoWithCA & HashMask;
		if (!isOccupied(key))
			return (icaoWithCA == 0x0) ? Iterator(key) : Iterator();

		return (m_table[key].icao == icaoWithCA) ? Iterator(key) : Iterator();
	}

	Iterator find(uint32_t icao) const noexcept {
		const auto key = icao & HashMask;
		if (!isOccupied(key))
			return (icao == 0x0) ? Iterator(key) : Iterator();

		return ((m_table[key].icao & 0xffffffu) == icao) ? Iterator(key) : Iterator();
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

	// The trust door asks a new address for a second sighting instead of
	// noise-floor evidence. A transponder squitters at 2 Hz, so two sightings
	// of the same address within thirty seconds are routine; noise clearing
	// 24 parity bits twice on the same random address is not. The window
	// matches the trust TTL, and sparse emitters (TIS-B at a fraction of a
	// hertz) still make the pair. A persistently weak aircraft opens the
	// door on its second squitter, and stays in.
	static constexpr uint32_t TrustCandidateMinTicks { 100 };
	static constexpr uint32_t TrustCandidateMaxTicks { 30'000'000 };

	bool confirmTrustCandidate(uint32_t icaoWithCA) noexcept {
		auto& candidate = m_trustCandidates[trustCandidateIndex(icaoWithCA)];
		const auto age = m_df11Clock - candidate.firstSeen;

		if (candidate.icaoWithCA != 0 && candidate.icaoWithCA == icaoWithCA) {
			if (age >= TrustCandidateMinTicks && age <= TrustCandidateMaxTicks) {
				candidate = TrustCandidate{};
				return true;
			}
			if (age < TrustCandidateMinTicks)
				return false;
		}

		candidate = TrustCandidate{icaoWithCA, m_df11Clock};
		return false;
	}

	bool confirmRejectedShort(uint32_t icao, uint64_t frame) noexcept {
		return confirmRejectedFrame(icao, 0, frame, 56);
	}

	bool confirmRejectedLong(uint32_t icao, uint64_t high, uint64_t low) noexcept {
		return confirmRejectedFrame(icao, high, low, 112);
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
		if (m_time1Mhz >= (0x1 << NumBits))
			return;

		// do a tick for the next entry otherwise
		doTickForEntry(m_time1Mhz);
	}

	/// Membership test against an 8 KB bitmap mirroring "this slot currently
	/// holds a trusted entry". The table itself is 65536 entries of 8 bytes, so
	/// a probe is an L2/DRAM access; callers that test addresses at demodulation
	/// rate need a filter that stays in L1.
	///
	/// Conservative by construction: markAsTrustedSeen() sets the bit
	/// immediately, so it is never clear for a trusted entry. A stale set bit is
	/// harmless because callers still confirm with findWithCA(), and the tick
	/// sweep clears it within one pass over the table.
	bool maybeTrusted(uint32_t icaoWithCA) const noexcept {
		const auto key = icaoWithCA & HashMask;
		return (m_trustedBits[key >> 6] >> (key & 63)) & 0x1;
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

	bool checkAltitude(const Iterator& entry, int32_t newAlt, bool updateState = true) noexcept {
		auto& state = m_squawkAlt[entry.key];
		if (state.altitude_25ft == AltitudeUnset) {
			if (updateState)
				state.altitude_25ft = newAlt / 25;
			return false;
		}
		if (!updateState && state.altitude_cnt == 0)
			return false;

		const auto delta = std::abs((int32_t)state.altitude_25ft * 25 - newAlt);
		if ((delta <= ALT_delta_ft)) {
			if (updateState) {
				state.altitude_25ft = newAlt / 25;
				state.altitude_cnt = 1;
			}
			return true;
		};

		if (!updateState)
			return false;

		if (state.altitude_cnt == 0) {
			state.altitude_25ft = newAlt / 25;
		} else {
			state.altitude_cnt = 0;
		}
		return false;
	}

	// Seed the per-aircraft CPR pair from a CRC-clean airborne position frame.
	// When an odd/even pair within fitWindow lands close in time, the decoded
	// position becomes the reference the repair gate checks against.
	void noteCprClean(const Iterator& entry, bool odd, uint32_t latCpr,
			uint32_t lonCpr, uint64_t now, uint64_t pairWindow) noexcept {
		auto& state = m_positionState[entry.key];
		if (odd) {
			state.cpr_odd_lat = latCpr;
			state.cpr_odd_lon = lonCpr;
			state.cpr_odd_time = now;
		} else {
			state.cpr_even_lat = latCpr;
			state.cpr_even_lon = lonCpr;
			state.cpr_even_time = now;
		}

		const uint64_t otherTime = odd ? state.cpr_even_time : state.cpr_odd_time;
		if (otherTime == 0 || now - otherTime > pairWindow)
			return;

		double lat = 0.0;
		double lon = 0.0;
		// the frame that just arrived is the more recent one, so the fix is
		// recorded for its parity's solution; recording the older parity's
		// solution would stamp a past position with the current time
		if (!ModeS::decodeCprGlobal(state.cpr_even_lat, state.cpr_even_lon,
				state.cpr_odd_lat, state.cpr_odd_lon, odd, lat, lon))
			return;
		state.last_lat_e5 = int32_t(lat * 1e5);
		state.last_lon_e5 = int32_t(lon * 1e5);
		state.position_time = now;
	}

	// Track what a downstream decoder actually received. Repaired positions
	// can be safe individually but incompatible with one another as a global
	// pair, so they must participate in the next opposite-parity check too.
	void noteCprOutput(const Iterator& entry, bool odd, uint32_t latCpr,
			uint32_t lonCpr, uint64_t now) noexcept {
		auto& state = m_positionState[entry.key];
		if (odd) {
			state.output_odd_lat = latCpr;
			state.output_odd_lon = lonCpr;
			state.output_odd_time = now;
		} else {
			state.output_even_lat = latCpr;
			state.output_even_lon = lonCpr;
			state.output_even_time = now;
		}
	}

	bool cachedPosition(const Iterator& entry, int32_t& latE5, int32_t& lonE5,
			uint64_t now, uint64_t maxAge) const noexcept {
		const auto& state = m_positionState[entry.key];
		if (state.position_time == 0 || now - state.position_time > maxAge)
			return false;
		latE5 = state.last_lat_e5;
		lonE5 = state.last_lon_e5;
		return true;
	}

	// The last emitted CPR bits of the opposite parity, when they are fresh
	// enough to pair globally with a frame that just arrived. This is the pair
	// a downstream receiver would form, so a repaired frame must be validated
	// against exactly it: its raw CPR bits go out unchanged.
	bool cachedOppositeCpr(const Iterator& entry, bool odd, uint32_t& latCpr,
			uint32_t& lonCpr, uint64_t now, uint64_t maxAge) const noexcept {
		const auto& state = m_positionState[entry.key];
		const uint64_t otherTime = odd
			? state.output_even_time : state.output_odd_time;
		if (otherTime == 0 || now - otherTime > maxAge)
			return false;
		latCpr = odd ? state.output_even_lat : state.output_odd_lat;
		lonCpr = odd ? state.output_even_lon : state.output_odd_lon;
		return true;
	}

	MsgStatEntry& getMsgStatEntry(const Iterator& it) noexcept {
		return m_msgStatTable[it.key];
	}
private:
	struct RejectedFrameCandidate {
		uint64_t high { 0 };
		uint64_t low { 0 };
		uint64_t firstSeen { 0 };
		uint32_t icao { 0 };
		uint8_t bits { 0 };
	};

	static constexpr size_t RejectedCandidateCount { 2048 };

	// Full-frame identity makes confirmation exact. This is direct-mapped so a
	// collision can replace a candidate, but can never confirm a different one.
	bool confirmRejectedFrame(uint32_t icao, uint64_t high, uint64_t low,
			uint8_t bits) noexcept {
		const uint64_t mixed = low ^ (high * 0x9e3779b97f4a7c15ull)
			^ (uint64_t(icao) * 0xbf58476d1ce4e5b9ull);
		auto& candidate = m_rejectedCandidates[mixed & (RejectedCandidateCount - 1)];
		const auto age = m_df11Clock - candidate.firstSeen;

		if (candidate.bits == bits && candidate.icao == icao
				&& candidate.high == high && candidate.low == low) {
			if (age >= RejectedCandidateMinTicks && age <= RejectedCandidateMaxTicks) {
				candidate.firstSeen = m_df11Clock;
				return true;
			}
			if (age < RejectedCandidateMinTicks)
				return false;
		}

		candidate = RejectedFrameCandidate{high, low, m_df11Clock, icao, bits};
		return false;
	}

	struct DF11Candidate {
		uint32_t icaoWithCA { 0 };
		uint64_t firstSeen { 0 };
	};

	struct TrustCandidate {
		uint32_t icaoWithCA { 0 };
		uint64_t firstSeen { 0 };
	};

	static constexpr size_t df11CandidateIndex(uint32_t icaoWithCA) noexcept {
		return (icaoWithCA * 0x9e3779b1u) >> 24;
	}

	static constexpr size_t trustCandidateIndex(uint32_t icaoWithCA) noexcept {
		return (icaoWithCA * 0x9e3779b1u) >> 23;
	}

	void setTrustedBit(uint32_t key) noexcept {
		m_trustedBits[key >> 6] |= (uint64_t(1) << (key & 63));
	}

	void clearTrustedBit(uint32_t key) noexcept {
		m_trustedBits[key >> 6] &= ~(uint64_t(1) << (key & 63));
	}

	std::array<uint64_t, Size / 64> m_trustedBits{};

	// mirrors m_table[key].icao != 0, kept in step by insertWithCA and
	// doResetEntry, the only two places that write the field
	bool isOccupied(uint32_t key) const noexcept {
		return (m_occupiedBits[key >> 6] >> (key & 63)) & 0x1;
	}

	void setOccupiedBit(uint32_t key) noexcept {
		m_occupiedBits[key >> 6] |= (uint64_t(1) << (key & 63));
	}

	void clearOccupiedBit(uint32_t key) noexcept {
		m_occupiedBits[key >> 6] &= ~(uint64_t(1) << (key & 63));
	}

	std::array<uint64_t, Size / 64> m_occupiedBits{};

	void doTickForEntry(uint16_t index) noexcept {
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

	void doResetEntry(uint16_t index) noexcept {
		auto& entry = m_table[index];
		entry.icao = 0x0;
		entry.ttl_trusted = 0;
		entry.ttl = 0;
		clearOccupiedBit(index);
		m_msgStatTable[index].last_time = 0;
		m_squawkAlt[index] = SquawkAlt{0, 0, 0, AltitudeUnset};
		m_positionState[index] = PositionState{};
	}

	
	// runs from 0 to 999 999
	uint32_t m_time1Mhz { 0 };
	uint64_t m_df11Clock { 0 };
	std::array<DF11Candidate, 256> m_df11Candidates{};
	std::array<TrustCandidate, 512> m_trustCandidates{};
	std::array<RejectedFrameCandidate, RejectedCandidateCount> m_rejectedCandidates{};

    // the table with the icao addresses including transponder CA 
	std::unique_ptr<Entry[]> m_table;

	// the table for the squawk and altitude data  
	std::unique_ptr<SquawkAlt[]> m_squawkAlt;

	// CPR history is cold state; keep it out of the hot squawk/altitude table.
	std::unique_ptr<PositionState[]> m_positionState;

	// the table with the msg timestamps
	std::unique_ptr<MsgStatEntry[]> m_msgStatTable;
};
