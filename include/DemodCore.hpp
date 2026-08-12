/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "Bits128.hpp"
#include "CRC.hpp"
#include "ErasureRepair.hpp"
#include "CRCErrorTable.hpp"
#include "ModeS.hpp"
#include "ICAOCache.hpp"
#include "Stats.hpp"
#include <bit>
#include <cmath>
#include "ShiftRegisters.hpp"
#include "MessageHandler.hpp"

template<int NumStreams, MessageHandler Handler>
class DemodCore {
public:
	// default constructor
	explicit DemodCore(Handler& messageHandler) : m_messageHandler(messageHandler) {
		// nothing
	}

	~DemodCore() {
		#if defined(STATS_ENABLED) && STATS_ENABLED
		#if defined(STATS_END_ONLY) && STATS_END_ONLY
			Stats::printStatsOnExit(m_statsLog, std::cerr);
		#endif
		#endif
	}

	// This is the main entry function called by the SampleStream. 
	// NumStreams many new bits are shifted in. The crc's are updated
	// and the streams are being checked for new messages
	using ConfidenceFn = float(*)(const void*, uint8_t, size_t);

	/// Installed by SampleStream so a repair can ask for the demodulator's
	/// per-bit confidence without the demodulation loop having to store it.
	void setConfidenceSource(const void* ctx, ConfidenceFn fn) noexcept {
		m_confidenceCtx = ctx;
		m_confidenceFn = fn;
	}

	void shiftInNewBits(uint32_t* cmp) {
		// the shift registers tell us which streams ended up on a downlink
		// format we handle. That is a rare event, so most calls leave here
		// without touching the dispatcher at all.
		uint64_t handledStreams = m_shiftRegisters.shiftInNewBits(cmp);
		// the streams and crc's are ready
		m_cache.tick();

		const uint64_t baseTime = m_currTime;
		while (handledStreams) {
			const auto i = std::countr_zero(handledStreams);
			// handleStream() looks at m_currTime, so keep it in step
			m_currTime = baseTime + i;
			handleStream(i);
			handledStreams &= handledStreams - 1;
		}
		m_currTime = baseTime + NumStreams;

		logStats(Stats::NUM_ITERATIONS);
	}

	bool sendFrameLongAligned(int,
							  const uint8_t downlinkFormat, 
							  CRC::crc_t crc,
							  const Bits128& frame, 
							  const ICAOTable::Iterator& it) {
		auto& e = m_cache.getMsgStatEntry(it);
		static constexpr uint64_t DUP_WINDOW_TICKS = 30 * NumStreams;
		if ((m_currTime - e.last_time) < DUP_WINDOW_TICKS) {
		    e.last_time = m_currTime;
    		logStatsDup(downlinkFormat);
    		return false;
		}

		if ((downlinkFormat == 20) || (downlinkFormat == 16)) {
			const auto alt_bits = ModeS::extractSquawkAlt_Long(frame);
			const auto alt = ModeS::decodeAltitude(alt_bits);
			const bool altitudeAccepted = alt
				&& m_cache.checkAltitude(it, *alt);
			if (!altitudeAccepted
					&& !m_cache.confirmRejectedLong(crc, frame.high(), frame.low())) {
				return false;
			}
			m_cache.markAsSeen(it);
		}
		
		if (downlinkFormat == 21) {
			const auto sqwk = ModeS::extractSquawkAlt_Long(frame);
			if (!m_cache.checkSquawk(it, sqwk)
					&& !m_cache.confirmRejectedLong(crc, frame.high(), frame.low()))
				return false;
			m_cache.markAsSeen(it);
		}
		
		logStatsSent(downlinkFormat);
		e.last_time = m_currTime;		
		m_messageHandler.handleLong(m_currTime, frame);
		return true;
	}

	bool sendFrameShortAligned(int, const uint8_t downlinkFormat, CRC::crc_t crc, const uint64_t& frameShort, const ICAOTable::Iterator& it) {
		auto& e = m_cache.getMsgStatEntry(it);
		static constexpr uint64_t DUP_WINDOW_TICKS = 30 * NumStreams;
		if ((m_currTime - e.last_time) < DUP_WINDOW_TICKS) {
		    e.last_time = m_currTime;
    		logStatsDup(downlinkFormat);
    		return false;
		}

		if ((downlinkFormat == 4) || (downlinkFormat == 0)) {
			const auto alt_bits = ModeS::extractSquawkAlt_Short(frameShort);
			const auto alt = ModeS::decodeAltitude(alt_bits);
			const bool altitudeAccepted = alt
				&& m_cache.checkAltitude(it, *alt);
			if (!altitudeAccepted
					&& !m_cache.confirmRejectedShort(crc, frameShort)) {
				return false;
			}
			m_cache.markAsSeen(it);
		}

		if (downlinkFormat == 5) {
			const auto sqwk = ModeS::extractSquawkAlt_Short(frameShort);
			if (!m_cache.checkSquawk(it, sqwk)
					&& !m_cache.confirmRejectedShort(crc, frameShort))
				return false;
			m_cache.markAsSeen(it);
		}

		logStatsSent(downlinkFormat);
		e.last_time = m_currTime;
		m_messageHandler.handleShort(m_currTime, frameShort);
		return true;
	}

	bool phaseDupCheckShort(const uint64_t& frameShort) noexcept {
		if (frameShort == m_prevShortFrame)
			return true;
		
		m_prevShortFrame = frameShort;
		return false;
	}

	bool phaseDupCheckLong(const Bits128& frameLong) noexcept {
		if (frameLong == m_prevLongFrame)
			return true;
		
		m_prevLongFrame = frameLong;
		return false;
	}

	// Dispatcher function for handling messages based on the downlink format  
	bool handleStream(int streamIndex) {
		const auto downlinkFormat = m_shiftRegisters.getDF(streamIndex);
		
		switch (downlinkFormat)
		{
		case 0: // acas
		case 4: // surveillance altitude
		case 5: // surveillance identity
			return handleAcasSurvShortMessage(streamIndex, downlinkFormat);
		case 11: // DF 11 messages
			return handleDF11ShortMessage(streamIndex);

		// Extended squitter messages
		case 17:
		case 18:
		case 19:
			return handleExtSquitterLongMessage(streamIndex, downlinkFormat);
		//  ACAS, Comm-B Messages
		case 16:
		case 20:
		case 21:
			return handleAcasCommBLongMessage(streamIndex, downlinkFormat);
		default:
			break;
		}

		return false;
	}

	/// @brief Handler for the extended squitter messages
	/// @return returns true if a message has been send to the output
	bool handleExtSquitterLongMessage(int streamIndex, const uint8_t& downlinkFormat) {
		auto frame = m_shiftRegisters.extractAlignedFrameLong(streamIndex);

		if (phaseDupCheckLong(frame))
			return false;

		auto crc = m_shiftRegisters.getCRC_112(streamIndex);

		// if the crc is zero, we have a correct message
		if (crc == 0) {
			// we consider a crc of 0 as a good message
			logStats(Stats::DF17_GOOD_MESSAGE);
			// get the address including the CA field
			const auto icaoWithCA = ModeS::extractICAOWithCA_Long(frame);
			const auto e = m_cache.findWithCA(icaoWithCA);
			
			// if we know this plane
			if (e.isValid()) {
				m_cache.markAsTrustedSeen(e);
				// and send the 112 bit message to the output
				return sendFrameLongAligned(streamIndex, downlinkFormat, crc, frame, e);
			} else {
				m_cache.markAsTrustedSeen(m_cache.insertWithCA(icaoWithCA));
			} 
		} else {
			// the crc is not zero, so we might have a broken message
			logStats(Stats::DF17_BAD_MESSAGE);
			// Ask the error table for a possible fix
			const auto fix_op = CRC::df17ErrorTable.lookup(crc);
			// can we fix it?
			if (fix_op.valid()) {
				// make a copy of the broken message
				Bits128 toRepair{ frame };
				// and let the error table apply the fix
				CRC::applyFixOp(fix_op, toRepair, 0);
				// extract the address together with the CA bits
				const auto icaoWithCA = ModeS::extractICAOWithCA_Long(toRepair);
				// do we know this icao address? We are only asking there the list of trusted addresses
				// using a not trusted address and repairing at the same time is too dangerous
				const auto e = m_cache.findWithCA(icaoWithCA);

				// if this plane is not known we are leaving this
				if (!e.isValid())
					return false;

				if (m_cache.isTrusted(e)) {
					// log that fixing the message was a success
					logStats(Stats::DF17_REPAIR_SUCCESS);
					// and keep the trusted entry alive
					m_cache.markAsTrustedSeen(e);
					// send the 112 bit message to the output
					return sendFrameLongAligned(streamIndex, downlinkFormat, crc, toRepair, e);
				};				
			}
			// The address must belong to a trusted aircraft for a repair to be
			// accepted at all, so test that first. maybeTrusted() is a single
			// L1-resident load; this runs at every DF17 header position, where a
			// full cache probe would be a memory stall.
			const auto icaoBefore = ModeS::extractICAOWithCA_Long(frame);
			if (m_cache.maybeTrusted(icaoBefore)
					&& tryErasureRepairLong(streamIndex, downlinkFormat, frame, crc, icaoBefore))
				return true;
			logStats(Stats::DF17_REPAIR_FAILED);
		}
		return false;
	}

	/// @brief Repair a damaged extended squitter by solving for which of the
	/// least confident bits were flipped. Acceptance is the same as the error
	/// table path: the recovered address must be a trusted aircraft.
	/// @return returns true if a message has been send to the output
	bool tryErasureRepairLong(int streamIndex, const uint8_t& downlinkFormat,
							  const Bits128& frame, CRC::crc_t crc, uint32_t icaoBefore) {
		if (m_confidenceFn == nullptr)
			return false;

		// maybeTrusted() can report a stale slot, so confirm against the table
		const auto before = m_cache.findWithCA(icaoBefore);
		if (!before.isValid() || !m_cache.isTrusted(before))
			return false;

		// collect the least confident bits, excluding the DF field
		constexpr uint8_t SearchLimit = 112 - 5;
		constexpr int k = ErasureRepair::Candidates;
		uint8_t candidates[k];
		float confidences[k];
		int count = 0;

		for (uint8_t bit = 0; bit < SearchLimit; ++bit) {
			const float c = m_confidenceFn(m_confidenceCtx, bit, size_t(streamIndex));
			if (count == k && c >= confidences[k - 1])
				continue;
			int j = (count < k) ? count++ : k - 1;
			for (; j > 0 && confidences[j - 1] > c; --j) {
				confidences[j] = confidences[j - 1];
				candidates[j] = candidates[j - 1];
			}
			confidences[j] = c;
			candidates[j] = bit;
		}

		const auto solution = ErasureRepair::solve(crc, candidates, size_t(count));
		if (!solution.solved)
			return false;

		// Real damage is a few bits; a spurious solve over this many
		// candidates flips about half of them. The weight cap is what pays
		// for the width of the candidate window.
		if (__builtin_popcount(solution.mask) > ErasureRepair::MaxWeight)
			return false;

		Bits128 repaired{ frame };
		for (int i = 0; i < count; ++i) {
			if (!((solution.mask >> i) & 1))
				continue;
			Bits128 flip(uint64_t(1));
			flip.shiftLeft(candidates[i]);
			repaired = repaired ^ flip;
		}

		// repairing may have moved the address; it must still be trusted
		const auto icaoWithCA = ModeS::extractICAOWithCA_Long(repaired);
		const auto e = m_cache.findWithCA(icaoWithCA);
		if (!e.isValid() || !m_cache.isTrusted(e))
			return false;

		logStats(Stats::DF17_REPAIR_SUCCESS);
		m_cache.markAsTrustedSeen(e);
		return sendFrameLongAligned(streamIndex, downlinkFormat, crc, repaired, e);
	}

	/// @brief Handler for long ACAS and Comm-B messages
	/// @return returns true if a message has been send to the output
	bool handleAcasCommBLongMessage(int streamIndex, const uint8_t& downlinkFormat) {
		auto frame = m_shiftRegisters.extractAlignedFrameLong(streamIndex);

		if (phaseDupCheckLong(frame))
			return false;

		auto crc = m_shiftRegisters.getCRC_112(streamIndex);
		// a valid message has the icao overlaid, i.e., check if crc corresponds to 
		// a known, active and trusted address
		if (crc ==  0)
			return false;
		const auto e = m_cache.find(crc);
		// if this is not in the list of known planes, we have to leave
		if (!e.isValid()) {
			return false;
		}

		if (m_cache.isAlive(e)) {
			// log that this message is a good message
			logStats(Stats::COMM_B_GOOD_MESSAGE);
			// output the message
			return sendFrameLongAligned(streamIndex, downlinkFormat, crc, frame, e);
		}

		return false;
	}

	/// @brief This function handles the downlink formats 0 (short acas reply), 4 (altitude reply), and 5 (identity reply)
	/// @return returns true if a message has been send to the output
	bool handleAcasSurvShortMessage(int streamIndex, const uint8_t& downlinkFormat) {
		// get the short message frame
		const auto frameShort = m_shiftRegisters.extractAlignedFrameShort(streamIndex);
		// first check if we have seen this in the previous stream
		if (phaseDupCheckShort(frameShort))
			return false;

		const auto crc = m_shiftRegisters.getCRC_56(streamIndex);

		if (crc ==  0)
			return false;
		// for DF 0, 4, 5 we have address parity, i.e. the crc of a valid message corresponds to the address of the transponder
		// check if we have a trustworthy address in our cache
		const auto e = m_cache.find(crc);
		// if this is not in the list of known planes, we have to leave
		if (!e.isValid())
			return false;

		if (m_cache.isAlive(e)) {
			// log that this message is a good message
			logStats(Stats::ACAS_SURV_GOOD_MESSAGE);
			// output the message
			return sendFrameShortAligned(streamIndex, downlinkFormat, crc, frameShort, e);		
		} 
		return false;
	}

	/// @brief Helper function for all-call replies (DF11) with a crc of zero. Either received correctly or repaired with 1-bit error correction
	/// @return returns true if a message has been send to the output
	bool handleDF11ShortMessageWithZeroCRC(int streamIndex, const uint64_t& frameShort, bool repaired) {
		const auto icaoWithCA = ModeS::extractICAOWithCA_Short(frameShort);
		const auto e = m_cache.findWithCA(icaoWithCA);
		
		// if the plane is not in table,
		if (!e.isValid()) {
			// put it there, but make sure this is not some repaired msg and 
			// that we are not resetting an existing entry with the same icao.
			if (!repaired && !m_cache.find(icaoWithCA & 0xFFFFFF).isValid()) {
				// insert and mark it as seen.
				const auto it = m_cache.insertWithCA(icaoWithCA);
				m_cache.markAsSeen(it);
				m_cache.getMsgStatEntry(it).last_time = m_currTime;
			}
			// we stop here and do not send the message
			return false;
		}

		if (m_cache.isAlive(e)) {
			// log that this message is a good message
			// we consider this a valid message
			m_cache.markAsSeen(e);
			// and output the message
			return sendFrameShortAligned(streamIndex, 11, 0, frameShort, e);
		} 
		m_cache.markAsSeen(e);
		return false;
	}

	/// @brief This function handles all-call replies (downlink format 11).
	/// @return returns true if a message has been send to the output
	bool handleDF11ShortMessage(int streamIndex) {
		auto frameShort = m_shiftRegisters.extractAlignedFrameShort(streamIndex);

		if (phaseDupCheckShort(frameShort))
			return false;

		const auto crc = m_shiftRegisters.getCRC_56(streamIndex);
	
		if (crc == 0) {
			logStats(Stats::DF11_ICAO_CA_FOUND_GOOD_CRC);
			return handleDF11ShortMessageWithZeroCRC(streamIndex, frameShort, false);
		} else if (crc < 80) {
			// PI is parity overlaid with the interrogator code (II/SI). Require
			// a second, separate sighting before adding a new address to the cache.
			const auto icaoWithCA = ModeS::extractICAOWithCA_Short(frameShort);
			if (!m_cache.findWithCA(icaoWithCA).isValid()
					&& !m_cache.confirmDF11Candidate(icaoWithCA))
				return false;

			logStats(Stats::DF11_ICAO_CA_FOUND_GOOD_CRC);
			return handleDF11ShortMessageWithZeroCRC(streamIndex, frameShort ^ crc, false);
		} else  {
			// ask the 1 bit error correction table for short messages for help
			const auto fix_op = CRC::df11ErrorTable.lookup(crc);
			// can we fix it?
			if (fix_op.valid()) {
				// let the error table apply the fix
				CRC::applyFixOp(fix_op, frameShort, 0);
				logStats(Stats::DF11_ICAO_CA_FOUND_1_BIT_FIX);
				// we are good now and proceed as with the normal zero crc case
				return handleDF11ShortMessageWithZeroCRC(streamIndex, frameShort, true);
			} else {
				// the crc is not good and no repairs with the error table. We do now a dirty trick here.
				// get the address including the CA field
				const auto icaoWithCA = ModeS::extractICAOWithCA_Short(frameShort);
				// look up the address in the trusted list
				const auto e = m_cache.findWithCA(icaoWithCA);
				// if it is there and we consider this as an active trusted transponder
				if (e.isValid() && m_cache.isTrusted(e)) {
					// Hence, we trust the address including the CA field. Downlink format is correct. 
					// make sure to have this sender address in the list of known but not thrustworthy addresses
					m_cache.markAsSeen(e);
					// The only remaining data in this short message is the parity block. Fix it and output the message
					return sendFrameShortAligned(streamIndex, 11, 0, frameShort ^ crc, e);
				}
			}
		}
		return false;
	} 


private:
	const void* m_confidenceCtx = nullptr;
	ConfidenceFn m_confidenceFn = nullptr;


#if defined(STATS_ENABLED) && STATS_ENABLED
	Stats::StatsLog m_statsLog;
	void logStats(Stats::EventType evt) {
		m_statsLog.log(evt);
		#if !(defined(STATS_END_ONLY) && STATS_END_ONLY)
			if (evt == Stats::NUM_ITERATIONS)
				Stats::printTick(m_statsLog, std::cerr);
		#endif
	}

	void logStatsSent(int df) {
		m_statsLog.logSent(df);
	}

	void logStatsDup(int df) {
		m_statsLog.logDup(df);
	}
#else
	void logStats(Stats::EventType) {}
	void logStatsSent(int) {}
	void logStatsDup(int) {}
#endif	
	static constexpr uint64_t samplesPerSecond() {
		return NumStreams * 1000000;
	}

	static constexpr uint64_t secondsToNumSamples(float secs) {
		return (samplesPerSecond() * secs);
	}
	
	// while dealing with a single stream, this holds a copy of the frame
	// from the previous stream  
	alignas(16) Bits128 m_prevLongFrame; 
	alignas(16) uint64_t m_prevShortFrame; 

	// plane lookup table
	ICAOTable m_cache; 
	
	// the current time measured in samples.
	uint64_t m_currTime{ 0 };
	
	// the shift registers for the bits
	ShiftRegisters<NumStreams> m_shiftRegisters;

	// the message handler that deals with long and short frames
	Handler& m_messageHandler;
};
