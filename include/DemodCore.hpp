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
#include "Plausibility.hpp"
#include "Logger.hpp"

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

	using PreambleFn = float(*)(const void*, size_t);
	void setPreambleSource(const void* ctx, PreambleFn fn) noexcept {
		m_preambleCtx = ctx;
		m_preambleFn = fn;
	}

	using SnrFn = float(*)(const void*, uint8_t);
	void setSnrSource(const void* ctx, SnrFn fn) noexcept {
		m_snrCtx = ctx;
		m_snrFn = fn;
	}

#ifndef STREAM1090_MIN_SNR
#define STREAM1090_MIN_SNR 0
#endif
	static constexpr float MinSnr = STREAM1090_MIN_SNR;

	/// True when the frame's signal is not clearly above its local noise floor.
	/// This is a ratio, so it transfers between receivers; 0 disables the test.
	bool signalAtNoiseFloor(int) noexcept {
		if constexpr (MinSnr <= 0.0f)
			return false;
		if (m_snrFn == nullptr)
			return false;
		return m_snrFn(m_snrCtx, 56) < MinSnr;
	}

	/// True when the preamble in front of this candidate is strong enough to be
	/// a real transmission. Disabled unless STREAM1090_PREAMBLE_GATE is defined.
	bool preambleConfirms([[maybe_unused]] int streamIndex) noexcept {
	#if defined(STREAM1090_PREAMBLE_GATE) && STREAM1090_PREAMBLE_GATE
		if (m_preambleFn == nullptr)
			return false;
		return m_preambleFn(m_preambleCtx, size_t(streamIndex))
			>= float(STREAM1090_PREAMBLE_THRESHOLD);
	#else
		return false;
	#endif
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
			if (addressParityRejects(uint32_t(i))) {
				handledStreams &= handledStreams - 1;
				continue;
			}
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

		// Address-parity frames (DF16/20/21) carry no checkable CRC: they name
		// their address in the parity field, so a noise frame can claim any
		// address in the table without ever clearing a CRC. Trust is
		// repetition-based: only addresses that earned it through a second
		// sighting may emit here. DF17/18 reach here with a checkable CRC;
		// their address earned trust at the door or before.
		if (!m_cache.isTrusted(it))
			return false;

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

		// the frame passed every check, so its address stays trusted
		m_cache.markAsTrustedSeen(it);
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

		// DF0/4/5 recover the address from the parity and DF11 carries it in
		// the clear, so a noise frame can claim any address in the table
		// without ever clearing a CRC. Trust is repetition-based: only
		// addresses that earned it through a second sighting may emit here.
		if (!m_cache.isTrusted(it))
			return false;

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

		// the frame passed every check, so its address stays trusted
		m_cache.markAsTrustedSeen(it);
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

	/// Reject address-parity candidates whose syndrome is not a cached address
	/// before entering the dispatcher. Keep the phase deduplication state in
	/// step exactly as the corresponding handler would have done.
	bool addressParityRejects(uint32_t streamIndex) noexcept {
		const auto df = m_shiftRegisters.getDF(streamIndex);
		const bool isShort = (df == 0) || (df == 4) || (df == 5);
		const bool isLong = (df == 16) || (df == 20) || (df == 21);
		if (!isShort && !isLong)
			return false;

		const CRC::crc_t syndrome = isShort
			? m_shiftRegisters.getCRC_56(streamIndex)
			: m_shiftRegisters.getCRC_112(streamIndex);
		if (syndrome != 0 && m_cache.find(syndrome).isValid())
			return false;

		if (isShort)
			phaseDupCheckShort(m_shiftRegisters.extractAlignedFrameShort(streamIndex));
		else
			phaseDupCheckLong(m_shiftRegisters.extractAlignedFrameLong(streamIndex));
		return true;
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
	bool handleExtSquitterLongMessage(int streamIndex, uint8_t downlinkFormat) {
		auto frame = m_shiftRegisters.extractAlignedFrameLong(streamIndex);

		if (phaseDupCheckLong(frame))
			return false;

		auto crc = m_shiftRegisters.getCRC_112(streamIndex);

		// A frame that only reaches crc==0 by way of the DF19->17 guess below
		// is a repair, not a genuine clean reception; captured before the guess
		// overwrites downlinkFormat, so the two stay distinguishable.
		const bool wasDf19 = (downlinkFormat == 19);

		// This is very hacky. However, we do not know about DF-19 nor seems to be many decoders.
		// We will give it a try as a DF-17 message since 17 and 19 have hamming distance 1
		if (downlinkFormat==19) {
            // flip the bit so 19 -> 17
            frame.flip(108);
            // adjust the crc for the flipped bit
            crc = crc ^ CRC::delta<108>();
            // and for statistics
            downlinkFormat = 17;
        } 

		// if the crc is zero, we have a correct message
		if (crc == 0) {
			// we consider a crc of 0 as a good message
			logStats(Stats::DF17_GOOD_MESSAGE);
			// get the address including the CA field
			const auto icaoWithCA = ModeS::extractICAOWithCA_Long(frame);
			if ((icaoWithCA & 0xffffffu) == 0)
				return false;
			const auto e = m_cache.findWithCA(icaoWithCA);

			// A trusted aircraft may renew immediately. An untrusted cache
			// entry still needs a separate sighting before it can emit.
			if (e.isValid() && m_cache.isTrusted(e)) {
				m_cache.markAsTrustedSeen(e);
				// and send the 112 bit message to the output
				return sendFrameLongAligned(streamIndex, downlinkFormat, crc, frame, e);
			}

			// A frame that only reads crc==0 because of the DF19->17 guess is a
			// repair, not a genuine clean reception: for any address that is not
			// already trusted (the case just above), it must not seed trust, nor
			// register a trust-candidate sighting for a later clean reception to
			// complete. Recovery for an address that already has trust is handled
			// above and is unaffected.
			if (wasDf19)
				return false;

			// This is the only door into the trusted set for a genuinely new
			// address; an already-known-but-untrusted entry (e.g. from DF11)
			// was screened at its own insertion, so only a fresh insert needs
			// the check here.
			if (!e.isValid()) {
				if (!Plausibility::checkICAO(icaoWithCA & 0xFFFFFF)) {
					Log::debug("DemodCore") << "Trying to insert invalid icao from DF-17 " << std::hex << (icaoWithCA & 0xFFFFFF);
					return false;
				}
				// DF18's bits here are CF (Control Field), not DF17's CA: CF values
				// 1-3 are ordinary DF18 report types (e.g. CF=2 Fine TIS-B with a
				// real ICAO address), not "no ADS-B capability" as they would be
				// for DF17's CA. Only native DF17 reaches this check; converted
				// DF19 frames have already been emitted or rejected above.
				if (downlinkFormat != 18 && !Plausibility::checkDF17(frame)) {
					Log::debug("DemodCore") << "Trying to insert by wrong DF-17 message  " << std::hex << (icaoWithCA & 0xFFFFFF);
					return false;
				}
			}

			// The first sighting enters the cache untrusted. Promotion needs
			// another sighting at least 100 us later, even while that entry lives.
			const bool confirmed = m_cache.confirmTrustCandidate(icaoWithCA);
			const auto it = e.isValid() ? e : m_cache.insertWithCA(icaoWithCA);
			if (confirmed) {
				m_cache.markAsTrustedSeen(it);
				return sendFrameLongAligned(streamIndex, downlinkFormat, crc, frame, it);
			}
			m_cache.markAsSeen(it);
			return false;
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
		if ((icaoWithCA & 0xffffffu) == 0)
			return false;
		const auto e = m_cache.findWithCA(icaoWithCA);
		
		// if the plane is not in table,
		if (!e.isValid()) {
			// put it there, but make sure this is not some repaired msg and 
			// that we are not resetting an existing entry with the same icao.
			if (!repaired && !m_cache.find(icaoWithCA & 0xFFFFFF).isValid()) {
				if (!Plausibility::checkICAO(icaoWithCA & 0xFFFFFF)) {
					Log::debug("DemodCore") << "Trying to insert invalid icao from DF-11 " << std::hex << (icaoWithCA & 0xFFFFFF);
					return false;
				}
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
			// A 24-bit CRC passes on noise about once per 16M candidate windows,
			// and each such DF11 inserts an address that then licenses
			// address-parity frames of its own. A new address gets in untrusted
			// on the first sighting and becomes trusted on the second, like the
			// DF17 door: noise never repeats a random address, an aircraft
			// repeats all the time.
			const auto icaoWithCA = ModeS::extractICAOWithCA_Short(frameShort);
			if ((icaoWithCA & 0xffffffu) == 0)
				return false;
			const auto e = m_cache.findWithCA(icaoWithCA);
			if (e.isValid() && m_cache.isTrusted(e)) {
				// A trusted address can renew immediately. An untrusted entry
				// still has to satisfy the minimum separation below.
				m_cache.markAsTrustedSeen(e);
				logStats(Stats::DF11_ICAO_CA_FOUND_GOOD_CRC);
				return handleDF11ShortMessageWithZeroCRC(streamIndex, frameShort, false);
			}
			if (m_cache.confirmTrustCandidate(icaoWithCA)) {
				// only a fresh insert needs screening; an existing untrusted
				// entry was already screened when it was first inserted
				if (!e.isValid() && !Plausibility::checkICAO(icaoWithCA & 0xFFFFFF)) {
					Log::debug("DemodCore") << "Trying to insert invalid icao from DF-11 " << std::hex << (icaoWithCA & 0xFFFFFF);
					return false;
				}
				const auto it = e.isValid() ? e : m_cache.insertWithCA(icaoWithCA);
				m_cache.markAsTrustedSeen(it);
				logStats(Stats::DF11_ICAO_CA_FOUND_GOOD_CRC);
				return handleDF11ShortMessageWithZeroCRC(streamIndex, frameShort, false);
			}
			// first sighting: enter untrusted so the parity routes see the
			// address, but emit nothing and leave the repairs locked
			if (e.isValid()) {
				m_cache.markAsSeen(e);
			} else if (!m_cache.find(icaoWithCA & 0xFFFFFF).isValid()) {
				if (!Plausibility::checkICAO(icaoWithCA & 0xFFFFFF)) {
					Log::debug("DemodCore") << "Trying to insert invalid icao from DF-11 " << std::hex << (icaoWithCA & 0xFFFFFF);
					return false;
				}
				const auto it = m_cache.insertWithCA(icaoWithCA);
				m_cache.markAsSeen(it);
			}
			return false;
		} else if (crc < 80) {
			// PI is parity overlaid with the interrogator code (II/SI). Require
			// a second, separate sighting before promoting an address.
			const auto icaoWithCA = ModeS::extractICAOWithCA_Short(frameShort);
			if ((icaoWithCA & 0xffffffu) == 0)
				return false;
			const auto e = m_cache.findWithCA(icaoWithCA);
			if (!e.isValid() || !m_cache.isTrusted(e)) {
				// PI is parity overlaid with the interrogator code (II/SI): the
				// address is in the clear behind a small syndrome. One sighting
				// proves nothing, noise draws addresses all the time; two
				// sightings of the same address, even from different interrogators,
				// are an aircraft. Mark it trusted, inserting it if needed; the
				// confirming frame stays silent, like the first one, and the next
				// reply emits.
				if (!m_cache.confirmDF11Candidate(icaoWithCA))
					return false;
				// only a fresh insert needs screening; an existing untrusted
				// entry was already screened when it was first inserted
				if (!e.isValid() && !Plausibility::checkICAO(icaoWithCA & 0xFFFFFF)) {
					Log::debug("DemodCore") << "Trying to insert invalid icao from DF-11 " << std::hex << (icaoWithCA & 0xFFFFFF);
					return false;
				}
				const auto it = e.isValid() ? e : m_cache.insertWithCA(icaoWithCA);
				m_cache.markAsTrustedSeen(it);
				logStats(Stats::DF11_ICAO_CA_FOUND_GOOD_CRC);
				return false;
			}

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
	const void* m_preambleCtx = nullptr;
	PreambleFn m_preambleFn = nullptr;
	const void* m_snrCtx = nullptr;
	SnrFn m_snrFn = nullptr;


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
	alignas(16) uint64_t m_prevShortFrame{ 0 };

	// plane lookup table
	ICAOTable m_cache; 
	
	// the current time measured in samples.
	uint64_t m_currTime{ 0 };
	
	// the shift registers for the bits
	ShiftRegisters<NumStreams> m_shiftRegisters;

	// the message handler that deals with long and short frames
	Handler& m_messageHandler;
};
