/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "Bits128.hpp"
#include "CRC.hpp"
#include "CRCErrorTable.hpp"
#include "ModeS.hpp"
#include "ICAOCache.hpp"
#include "Stats.hpp"
#include <cmath>
#include "ShiftRegisters.hpp"
//#include "PlaneTable.hpp"

template<int NumStreams>
class DemodCore {
public:
	// default constructor
	DemodCore() {
		// nothing
	}

	~DemodCore() {
		#if defined(STATS_ENABLED) && STATS_ENABLED
		#if defined(STATS_END_ONLY) && STATS_END_ONLY
			Stats::printStatsOnExit(m_statsLog, std::cerr);
		#endif
		#endif
	}

	bool sendFrameLongAligned(const uint8_t downlinkFormat, CRC::crc_t, const Bits128& frame, const ICAOTable::Iterator& it) {
		if (((m_currTime - m_prevTimeLongSent) < NumStreams) && (m_prevFrameLongSent == frame)) {
			logStatsDup(downlinkFormat);
			return false;
		}

		if ((downlinkFormat == 20) || (downlinkFormat == 16)) {
			const auto alt_bits = ModeS::extractSquawkAlt_Long(frame);
			const auto alt = ModeS::decodeAltitude(alt_bits);
			//m_planeTable.upsertAlt(crc, alt);
			if (!m_cache.checkAltitude(it, alt)) {	
				return false;
			} else {
				m_cache.markAsSeen(it);
				//m_planeTable.upsertAlt(crc, alt);
			}
		}
		
		if (downlinkFormat == 21) {
			const auto sqwk = ModeS::extractSquawkAlt_Long(frame);
			if (!m_cache.checkSquawk(it, sqwk))
				return false;
			m_cache.markAsSeen(it);
			//m_planeTable.upsertSquawk(crc, sqwk);
		}
		
		logStatsSent(downlinkFormat);
		m_prevFrameLongSent = frame;
		m_prevTimeLongSent = m_currTime;


#if defined(OUTPUT_RAW) && OUTPUT_RAW
		ModeS::printFrameLongRaw(std::cout, frame);
#else
		ModeS::printFrameLongMLAT(std::cout, currTimeTo12MhzTimeStamp() + RegLayout::OffsetMLAT_Long , frame);
#endif
		return true;
	}

	bool sendFrameShortAligned(const uint8_t downlinkFormat, CRC::crc_t, const uint64_t& frameShort, const ICAOTable::Iterator& it) {
		if (((m_currTime - m_prevTimeShortSent) < NumStreams) && (m_prevFrameShortSent == frameShort)) {
			logStatsDup(downlinkFormat);
			return false;
		}

		if ((downlinkFormat == 4) || (downlinkFormat == 0)) {
			const auto alt_bits = ModeS::extractSquawkAlt_Short(frameShort);
			const auto alt = ModeS::decodeAltitude(alt_bits);
			if (!m_cache.checkAltitude(it, alt)) {
				return false;
			} else {
				m_cache.markAsSeen(it);
				//m_planeTable.upsertAlt(crc, alt);
			}
		}

		if (downlinkFormat == 5) {
			const auto sqwk = ModeS::extractSquawkAlt_Short(frameShort);
			if (!m_cache.checkSquawk(it, sqwk))
				return false;
			m_cache.markAsSeen(it);
			//m_planeTable.upsertSquawk(crc, sqwk);
		}
		
		logStatsSent(downlinkFormat);
		m_prevFrameShortSent = frameShort;
		m_prevTimeShortSent = m_currTime;

#if defined(OUTPUT_RAW) && OUTPUT_RAW
		ModeS::printFrameShortRaw(std::cout, frameShort);
#else 
		ModeS::printFrameShortMLAT(std::cout, currTimeTo12MhzTimeStamp() + RegLayout::OffsetMLAT_Short, frameShort);
#endif
		return true;
	}
	
	// This is the main entry function called by the SampleStream. 
	// NumStreams many new bits are shifted in. The crc's are updated
	// and the streams are being checked for new messages
	void shiftInNewBits(uint32_t* cmp) {
		m_shiftRegisters.shiftInNewBits(cmp); 
		// the streams and crc's are ready
		m_cache.tick();
		for (auto i = 0; i < NumStreams; i++) {
			if constexpr(RegLayout::SameStart) {
				handleStream(i);
			} else { 
				if (!handleStreamShort(i)) {
					handleStreamLong(i); 
				} 				
			}
			
			m_currTime++;
		}
		logStats(Stats::NUM_ITERATIONS);
		//m_planeTable.tick();
	}


	bool phaseDupCheckShort(const uint64_t& frameShort) {
		if (frameShort == m_prevShortFrame)
			return true;
		
		m_prevShortFrame = frameShort;
		return false;
	}

	bool phaseDupCheckLong(const Bits128& frameLong) {
		if (frameLong == m_prevLongFrame)
			return true;
		
		m_prevLongFrame = frameLong;
		return false;
	}

	bool handleStreamLong(int streamIndex) {	
		const auto downlinkFormat = m_shiftRegisters.getDF_112(streamIndex);
		switch (downlinkFormat)
		{
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

	bool handleStreamShort(int streamIndex) {

		const auto downlinkFormat = m_shiftRegisters.getDF_56(streamIndex);
		switch (downlinkFormat)
		{
		case 0: // acas
		case 4: // surveillance altitude
		case 5: // surveillance identity
			return handleAcasSurvShortMessage(streamIndex, downlinkFormat);
		case 11: // DF 11 messages
			return handleDF11ShortMessage(streamIndex);
		default:
			break;
		}
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
				return sendFrameLongAligned(downlinkFormat, crc, frame, e);
			} else {
				m_cache.markAsTrustedSeen(m_cache.insertWithCA(icaoWithCA));
			} /*
			if (e.isValid()) {
				// if we trust this address and the we have seen it not that long ago
				if (m_cache.isTrusted(e)) {
					// mark the plane as seen
					m_cache.markAsTrustedSeen(e);
					// and send the 112 bit message to the output
					return sendFrameLongAligned(downlinkFormat, crc, frame, e);
				} else if (m_cache.isAlive(e)) {
					// we have seen this entry before and it has been put there by DF11.
					// Good enough. Promote it and insert the address into the trusted table. 
					// -----------------------------------------------
					//  NOTE: this is the only place where an address
					//  can enter the list of trustworthy senders
					//  After many experiments, this turned out to be
					//  the way to go. 
					// ----------------------------------------------  
					m_cache.markAsTrustedSeen(e);
					// and send the 112 bit message to the output
					return sendFrameLongAligned(downlinkFormat, crc, frame, e);
				} 
			} else { 
				m_cache.markAsTrustedSeen(m_cache.insertWithCA(icaoWithCA));
				// and send the 112 bit message to the output
				return sendFrameLongAligned(downlinkFormat, crc, frame, e);
			}	*/
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
					return sendFrameLongAligned(downlinkFormat, crc, toRepair, e);
				} else {
					//return sendFrameLongAligned(downlinkFormat, crc, toRepair, e);
				}				
			}
			logStats(Stats::DF17_REPAIR_FAILED);
		}
		return false;
	}

	/// @brief Handler for long ACAS and Comm-B messages
	/// @param downlinkFormat the downlink format (16,20,21) 
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
			return sendFrameLongAligned(downlinkFormat, crc, frame, e);
		}

		return false;
	}

	

	/// @brief This function handles the downlink formats 0 (short acas reply), 4 (altitude reply), and 5 (identity reply)
	/// @param downlinkFormat the downlink format (0, 4, 5) 
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
			return sendFrameShortAligned(downlinkFormat, crc, frameShort, e);		
		} 
		return false;
	}

	/// @brief Helper function for all-call replies (DF11) with a crc of zero. Either received correctly or repaired with 1-bit error correction
	/// @return returns true if a message has been send to the output
	bool handleDF11ShortMessageWithZeroCRC(const uint64_t& frameShort, bool repaired) {
		const auto icaoWithCA = ModeS::extractICAOWithCA_Short(frameShort);
		const auto e = m_cache.findWithCA(icaoWithCA);
		
		// if the plane is not in table,
		if (!e.isValid()) {
			// put it there.
			if (!repaired) {
				m_cache.insertWithCA(icaoWithCA);
			}
			// we stop here and do not send the message
			return false;
		}

		if (m_cache.isAlive(e)) {
			// log that this message is a good message
			// we consider this a valid message
			m_cache.markAsSeen(e);
			// and output the message
			return sendFrameShortAligned(11, 0, frameShort, e);
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
			return handleDF11ShortMessageWithZeroCRC(frameShort, false);
		} else  {
			// ask the 1 bit error correction table for short messages for help
			const auto fix_op = CRC::df11ErrorTable.lookup(crc);
			// can we fix it?
			if (fix_op.valid()) {
				// let the error table apply the fix
				CRC::applyFixOp(fix_op, frameShort, 0);
				logStats(Stats::DF11_ICAO_CA_FOUND_1_BIT_FIX);
				// we are good now and proceed as with the normal zero crc case
				return handleDF11ShortMessageWithZeroCRC(frameShort, true);
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
					return sendFrameShortAligned(11, 0, frameShort ^ crc, e);
				}
			}
		}
		return false;
	} 


private:

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

	constexpr uint64_t currTimeTo12MhzTimeStamp() {
		constexpr double ratio = 12.0/(double)NumStreams;
		return (uint64_t)(m_currTime * ratio);
	}
	
	// while dealing with a single stream, this holds a copy of the frame
	// from the previous stream  
	Bits128 m_prevLongFrame;
	uint64_t m_prevShortFrame;

	// the last long message sent to the ouput. Used for duplicate removal
	Bits128 m_prevFrameLongSent;
	// the timestamp of the last long message sent. Required for duplicate removal
	uint64_t m_prevTimeLongSent { 0 };

	// the last short message sent to the ouput. Used for duplicate removal
	uint64_t m_prevFrameShortSent;
	// the timestamp of the last short message sent. Required for duplicate removal
	uint64_t m_prevTimeShortSent { 0 };

	// plane lookup table
	ICAOTable m_cache;

	// After this amount of seconds, a trusted address is not considered as valid anymore
	uint64_t m_trustedTimeOut { secondsToNumSamples(30.0) };

	// here we use a shorter timeout. After this amount of seconds, an address is no longer valid.
	uint64_t m_notTrustedTimeOut { secondsToNumSamples(2.0) };
	
	// the current time measured in samples.
	uint64_t m_currTime{ 0 };

#if defined(MSG_RIGHT_ALIGN) && MSG_RIGHT_ALIGN
	using RegLayout = RegisterLayout_Right;
#else
	using RegLayout = RegisterLayout_Left;
#endif
	
	ShiftRegisters<NumStreams, RegLayout> m_shiftRegisters;

	//PlaneTable m_planeTable;
};

template<>
constexpr uint64_t DemodCore<8>::currTimeTo12MhzTimeStamp() {
	// for 8 Mhz we have 1.5 * 8 = 12
	return m_currTime + (m_currTime >> 1);
}

template<>
constexpr uint64_t DemodCore<16>::currTimeTo12MhzTimeStamp() {
	// for 16 Mhz we have 0.75 * 16 = (0.5 + 0.25) * 16 = 12
	return (m_currTime >> 1) + (m_currTime >> 2);
}

template<>
constexpr uint64_t DemodCore<6>::currTimeTo12MhzTimeStamp() {
	// for 6 Mhz we have 2.0 * 6 = 12
	return (m_currTime << 1);
}

template<>
constexpr uint64_t DemodCore<12>::currTimeTo12MhzTimeStamp() {
	// for 12 Mhz nothing to do
	return m_currTime;
}

template<>
constexpr uint64_t DemodCore<24>::currTimeTo12MhzTimeStamp() {
	// for 24 Mhz we simply divide by 2
	return (m_currTime >> 1);
}

template<>
constexpr uint64_t DemodCore<48>::currTimeTo12MhzTimeStamp() {
	// for 48 Mhz we simply divide by 4
	return (m_currTime >> 2);
}

template<>
constexpr uint64_t DemodCore<10>::currTimeTo12MhzTimeStamp() {
	// for 10 Mhz we have 12/10 = 6/5 = 1 + 1/5
	return m_currTime + m_currTime/5;
}

template<>
constexpr uint64_t DemodCore<20>::currTimeTo12MhzTimeStamp() {
	// for 20 Mhz we have 12/20 = 6/10 = 1/2 + 1/10 
	return (m_currTime >> 1) + m_currTime/10;
}

template<>
constexpr uint64_t DemodCore<40>::currTimeTo12MhzTimeStamp() {
	// for 40 Mhz we have 12/40 = 3/10 = 1/4 + 1/20 
	return (m_currTime >> 2) + m_currTime/20;
}

