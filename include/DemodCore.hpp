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

template<int NumStreams>
class DemodCore {
public:
	// default constructor
	DemodCore() {
		m_prev_crc_112 = 0;
		m_prev_crc_56 = 0;
		for (auto i = 0; i < NumStreams; i++) {
			m_crc_112[i] = 0;
			m_crc_56[i] = 0;
		}
	}

	void sendFrameLong(const uint8_t downlinkFormat, CRC::crc_t, const Bits128& frame) {
		if (((m_currTime - m_prevTimeLongSent) < NumStreams) && ModeS::equalLong(frame, m_prevFrameLongSent)) {
			logStatsDup(downlinkFormat);
			return;
		}

		logStatsSent(downlinkFormat);
		m_prevFrameLongSent = frame;
		m_prevTimeLongSent = m_currTime;

#if defined(OUTPUT_RAW) && OUTPUT_RAW
		ModeS::printFrameLongRaw(std::cout, frame);
#else
		// every bit is a 1 tick @ 1 Mhz which corresponds to 12 ticks @12 Mhz.
		// the message started 112 ticks ago @ 1 Mhz => 112 * 12 ticks @ 12 Mhz ago. 
		ModeS::printFrameLongMlat(std::cout, currTimeTo12MhzTimeStamp() - (112 * 12), frame);
		//ModeS::printFrameLong(std::cout, frame);
#endif
	}

	void sendFrameShort(const uint8_t downlinkFormat, CRC::crc_t, const Bits128& frame) {
		if (((m_currTime - m_prevTimeShortSent) < NumStreams) && ModeS::equalShort(frame, m_prevFrameShortSent)) {
			logStatsDup(downlinkFormat);
			return;
		}
		
		logStatsSent(downlinkFormat);
		m_prevFrameShortSent = frame;
		m_prevTimeShortSent = m_currTime;

#if defined(OUTPUT_RAW) && OUTPUT_RAW
		ModeS::printFrameShortRaw(std::cout, frame);
#else 
	ModeS::printFrameShortMlat(std::cout, currTimeTo12MhzTimeStamp() - (56 * 12), frame);	
	//ModeS::printFrameShort(std::cout, frame);
#endif
	}
	
	// This is the main entry function called by the SampleStream. 
	// NumStreams many new bits are shifted in. The crc's are updated
	// and the streams are being checked for new messages
	void shiftInNewBits(uint32_t* cmp) {
		for (auto i = 0; i < NumStreams; i++) {
			// check if we shift out the 112th bit
			if (m_bits[i].high() & (0x1ull << 47)) {
				// adjust the 112 bit crc accordingly
				m_crc_112[i] ^= CRC::delta<111>();
			}

			// check if we shift out the 56th bit
			if (m_bits[i].low() & (0x1ull << 55)) {
				// adjust the 56 bit crc
				m_crc_56[i] ^= CRC::delta<55>();
			}

			// now shift the complete frame one bit to the left
			m_bits[i].shiftLeft();
			// and add the new bit
			m_bits[i].low() |= cmp[i];

			// do the same with the 56- and 112-bit crcs
			m_crc_112[i] = (m_crc_112[i] << 1) | cmp[i];
			m_crc_56[i]  = (m_crc_56[i]  << 1) | cmp[i];

			// if now the 25-th bit is set, divide by the polynomial 
			if (m_crc_112[i] & (0x1 << 24)) {
				m_crc_112[i] ^= CRC::polynomial;
			}

			// same for the crc of the 56 bits
			if (m_crc_56[i] & (0x1 << 24)) {
				m_crc_56[i]  ^= CRC::polynomial;
			}
		}

		// the streams and crc's are ready
		for (auto i = 0; i < NumStreams; i++) {
			// first check if there is a short message
			bool foundShortMessage = handleStreamShort(m_crc_56[i], m_bits[i]);
			// if we have found a short message, there is no need to check for a long message
			if (!foundShortMessage) {
				handleStreamLong(m_crc_112[i], m_bits[i]);
			}
				
			m_prevFrame = m_bits[i];
			m_prev_crc_112 = m_crc_112[i];
			m_prev_crc_56 = m_crc_56[i];
			m_currTime++;
		}
		logStats(Stats::NUM_ITERATIONS);
	}
	
	// Dispatcher function for handling 112-bit messages based on the downlink format  
	bool handleStreamLong(const CRC::crc_t &crc, const Bits128 &frame) {
		// check if the previous stream has dealt with this
		if ((crc == m_prev_crc_112) && (ModeS::equalLong(frame, m_prevFrame))) {
			// the other stream already has dealt with this content, broken or not.
			return false;
		}

		// extract the first 5 bits representing the DF
		const auto downlinkFormat = ModeS::extractDownlinkFormat112(frame);

		// consider cases based on the DF
		switch (downlinkFormat) {
		// Extended squitter messages
		case 17:
		case 18:
		case 19:
			return handleExtSquitterLongMessage(downlinkFormat, crc, frame);

		//  ACAS, Comm-B Messages
		case 16:
		case 20:
		case 21:
			return handleAcasCommBLongMessage(downlinkFormat, crc, frame);
		default:
			break;
		}
		return false;
	}

	// Dispatcher function for handling 56-bit messages based on the downlink format
	bool handleStreamShort(const CRC::crc_t& crc, const Bits128& frame) {
		// check if the previous stream has dealt with this
		if ((crc == m_prev_crc_56) && (ModeS::equalShort(frame, m_prevFrame))) {
			// the other stream already has dealt with this content, broken or not.
			return false;
		}

		// extract the downlink format from the frame
		const auto downlinkFormat = ModeS::extractDownlinkFormat56(frame);
		switch (downlinkFormat)
		{
		case 0: // acas
		case 4: // surveillance altitude
		case 5: // surveillance identity
			return handleAcasSurvShortMessage(downlinkFormat, crc, frame);
		case 11: // DF 11 messages
			return handleDF11ShortMessage(crc, frame);
		default:
			break;
		}
		return false;
	}

	

	/// @brief Handler for the extended squitter messages
	/// @param downlinkFormat the downlink format (17,18,19) 
	/// @param crc the crc of the current frame
	/// @param frame the frame itself
	/// @return returns true if a message has been send to the output
	bool handleExtSquitterLongMessage(const uint8_t& downlinkFormat, const CRC::crc_t& crc, const Bits128& frame) {
		// log this as an extended squitter message
		// logStats(Stats::DF17_HEADER);
		// if the crc is zero, we have a correct message
		if (crc == 0) {
			// we consider a crc of 0 as a good message
			logStats(Stats::DF17_GOOD_MESSAGE);
			// get the address including the CA field
			const auto icaoWithCA = ModeS::DF17_extractICAOWithCA(frame);
			
			const auto e = m_cache.findWithCA(icaoWithCA);
			
			// if we know this plane
			if (e.isValid()) {
				// if we trust this address and the we have seen it not that long ago
				if (m_cache.isTrusted(e) && m_cache.notOlderThan(e, m_currTime, m_trustedTimeOut)) {
					// mark the plane as seen
					m_cache.markAsSeen(e, m_currTime);
					// and send the 112 bit message to the output
					sendFrameLong(downlinkFormat, crc, frame);
					return true;
				} else if (m_cache.notOlderThan(e, m_currTime, m_notTrustedTimeOut)) {
					// we have seen this entry before and it has been put there by DF11.
					// Good enough. Promote it and insert the address into the trusted table. 
					// -----------------------------------------------
					//  NOTE: this is the only place where an address
					//  can enter the list of trustworthy senders
					//  After many experiments, this turned out to be
					//  the way to go. 
					// ---------------------------------------------- 
					m_cache.markAsTrusted(e);
					// mark the plane as seen
					m_cache.markAsSeen(e, m_currTime);
					// and send the 112 bit message to the output
					sendFrameLong(downlinkFormat, crc, frame);
					return true;
				}
			} else {
				m_cache.insertWithCA(icaoWithCA, m_currTime);
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
				CRC::applyFixOp(fix_op, toRepair);
				// extract the address together with the CA bits
				const auto icaoWithCA = ModeS::DF17_extractICAOWithCA(toRepair);
				// do we know this icao address? We are only asking there the list of trusted addresses
				// using a not trusted address and repairing at the same time is too dangerous
				const auto e = m_cache.findWithCA(icaoWithCA);
				if (e.isValid() && m_cache.isTrusted(e) && m_cache.notOlderThan(e, m_currTime, m_trustedTimeOut)) {
					// log that fixing the message was a success
					logStats(Stats::DF17_REPAIR_SUCCESS);
					// and keep the trusted entry alive
					m_cache.markAsSeen(e, m_currTime);
					// send the 112 bit message to the output
					sendFrameLong(downlinkFormat, crc, toRepair);
					return true;
				} 				
			}
			logStats(Stats::DF17_REPAIR_FAILED);
		}
		return false;
	}

	/// @brief Handler for long ACAS and Comm-B messages
	/// @param downlinkFormat the downlink format (16,20,21) 
	/// @param crc the crc of the current frame
	/// @param frame the frame itself
	/// @return returns true if a message has been send to the output
	bool handleAcasCommBLongMessage(const uint8_t& downlinkFormat, const CRC::crc_t& crc, const Bits128& frame) {
		// log the type of message
		// logStats(Stats::COMM_B_HEADER);
		// a valid message has the icao overlaid, i.e., check if crc corresponds to 
		// a known, active and trusted address
		const auto e = m_cache.find(crc);
		// if this is not in the list of known planes, we have to leave
		if (!e.isValid())
			return false;

		if (m_cache.notOlderThan(e, m_currTime, m_notTrustedTimeOut) || 
		   (m_cache.isTrusted(e) && m_cache.notOlderThan(e, m_currTime, m_trustedTimeOut))) {
			// log that this message is a good message
			logStats(Stats::ACAS_SURV_GOOD_MESSAGE);
			// we consider this a valid comm-b message
			m_cache.markAsSeen(e, m_currTime);
			// and output the message
			sendFrameLong(downlinkFormat, crc, frame);
			// we are done
			return true;
		} 
		return false;
	}

	

	/// @brief This function handles the downlink formats 0 (short acas reply), 4 (altitude reply), and 5 (identity reply)
	/// @param downlinkFormat the downlink format (0, 4, 5) 
	/// @param crc the crc of the current frame
	/// @param frame the frame itself
	/// @return returns true if a message has been send to the output
	bool handleAcasSurvShortMessage(const uint8_t& downlinkFormat, const CRC::crc_t& crc, const Bits128& frame) {
		// log the type of message
		//logStats(Stats::ACAS_SURV_HEADER);
		// for DF 0, 4, 5 we have address parity, i.e. the crc of a valid message corresponds to the address of the transponder
		// check if we have a trustworthy address in our cache
		const auto e = m_cache.find(crc);
		// if this is not in the list of known planes, we have to leave
		if (!e.isValid())
			return false;

		if (m_cache.notOlderThan(e, m_currTime, m_notTrustedTimeOut) || 
		   (m_cache.isTrusted(e) && m_cache.notOlderThan(e, m_currTime, m_trustedTimeOut))) {
			// log that this message is a good message
			logStats(Stats::ACAS_SURV_GOOD_MESSAGE);
			// we consider this a valid comm-b message
			m_cache.markAsSeen(e, m_currTime);
			// and output the message
			sendFrameShort(downlinkFormat, crc, frame);
			// we are done
			return true;
		} 
		return false;
	}

	/// @brief Helper function for all-call replies (DF11) with a crc of zero. Either received correctly or repaired with 1-bit error correction
	/// @param frame the frame itself
	/// @return returns true if a message has been send to the output
	bool handleDF11ShortMessageWithZeroCRC(const Bits128& frame) {
		// get the address including the CA field
		const auto icaoWithCA = ModeS::DF11_extractICAOWithCA(frame);
		
		const auto e = m_cache.findWithCA(icaoWithCA);
		
		// if the plane is not in table,
		if (!e.isValid()) {
			// put it there.
			m_cache.insertWithCA(icaoWithCA, m_currTime);
			// we stop here and do not send the message
			return false;
		}

		if (m_cache.notOlderThan(e, m_currTime, m_notTrustedTimeOut) || 
		   (m_cache.isTrusted(e) && m_cache.notOlderThan(e, m_currTime, m_trustedTimeOut))) {
			// log that this message is a good message
			logStats(Stats::ACAS_SURV_GOOD_MESSAGE);
			// we consider this a valid comm-b message
			m_cache.markAsSeen(e, m_currTime);
			// and output the message
			sendFrameShort(11, 0, frame);
			// we are done
			return true;
		} 
		m_cache.markAsSeen(e, m_currTime);
		return false;
	}

	/// @brief This function handles all-call replies (downlink format 11).
	/// @param crc the crc of the current frame
	/// @param frame the frame itself
	/// @return returns true if a message has been send to the output
	bool handleDF11ShortMessage(const CRC::crc_t& crc, const Bits128& frame) {
		// log this as a possible DF11 message
		//logStats(Stats::DF11_HEADER);

		if (crc == 0) {
			logStats(Stats::DF11_ICAO_CA_FOUND_GOOD_CRC);
			return handleDF11ShortMessageWithZeroCRC(frame);
		} else  {
			// ask the 1 bit error correction table for short messages for help
			const auto fix_op = CRC::df11ErrorTable.lookup(crc);
			// can we fix it?
			if (fix_op.valid()) {
				// make a copy of the broken message
				Bits128 toRepair{ frame };
				// and let the error table apply the fix
				CRC::applyFixOp(fix_op, toRepair);
				logStats(Stats::DF11_ICAO_CA_FOUND_1_BIT_FIX);
				// we are good now and proceed as with the normal zero crc case
				return handleDF11ShortMessageWithZeroCRC(toRepair);
			} else {
				// the crc is not good and no repairing with a 1 bit fix. We do now a dirty trick here.
				// get the address including the CA field
				const auto icaoWithCA = ModeS::DF11_extractICAOWithCA(frame);
				// look up the address in the trusted list
				const auto e = m_cache.findWithCA(icaoWithCA);
				// if it is there and we consider this as an active trusted transponder
				if (e.isValid() && m_cache.isTrusted(e) && m_cache.notOlderThan(e, m_currTime, m_trustedTimeOut)) {
					// Hence, we trust the address including the CA field. Downlink format is correct. 
					// The only remaining data in this short message is the parity block. Fix it!
					Bits128 toRepair{ frame };
					toRepair.low() ^= crc;
					// make sure to have this sender address in the list of known but not thrustworthy addresses
					m_cache.markAsSeen(e, m_currTime);
					// output the message
					sendFrameShort(11, 0, toRepair);
					// we are done here
					return true;
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
		if (evt == Stats::NUM_ITERATIONS)
			Stats::printTick(m_statsLog, std::cerr);
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
	
	// NumStreams many shift registers holding the current frames 
	Bits128 m_bits[NumStreams];

	// Each stream has a checksum for long messages (112 bit)
	CRC::crc_t m_crc_112[NumStreams];

	// And a checksum for the short messages (56 bit)
	CRC::crc_t m_crc_56[NumStreams];

	// while dealing with a single stream, this holds a copy of the frame
	// from the previous stream  
	Bits128 m_prevFrame;
	// a copy of the previous long message checksum
	CRC::crc_t m_prev_crc_112;
	// and a also the corresponding short message checksum 
	CRC::crc_t m_prev_crc_56;

	// the last long message sent to the ouput. Used for duplicate removal
	Bits128 m_prevFrameLongSent;
	// the timestamp of the last long message sent. Required for duplicate removal
	uint64_t m_prevTimeLongSent { 0 };

	// the last short message sent to the ouput. Used for duplicate removal
	Bits128 m_prevFrameShortSent;
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
};

template<>
inline uint64_t DemodCore<8>::currTimeTo12MhzTimeStamp() {
	// for 8 Mhz we have 1.5 * 8 = 12
	return m_currTime + (m_currTime >> 1);
}

template<>
inline uint64_t DemodCore<6>::currTimeTo12MhzTimeStamp() {
	// for 6 Mhz we have 2.0 * 6 = 12
	return (m_currTime << 1);
}

template<>
inline uint64_t DemodCore<12>::currTimeTo12MhzTimeStamp() {
	// for 12 Mhz nothing to do
	return m_currTime;
}

template<>
inline uint64_t DemodCore<10>::currTimeTo12MhzTimeStamp() {
	// for 12 Mhz nothing to do
	return m_currTime;
}