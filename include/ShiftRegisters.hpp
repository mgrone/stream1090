/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "Bits128.hpp"
#include "CRC.hpp"

template<int _ShortMessageStart, int _LongMessageStart>
struct RegisterLayout {
    static constexpr auto MessageStart_Short = _ShortMessageStart;
    static constexpr auto MessageStart_Long  = _LongMessageStart;

    static constexpr auto MessageEnd_Short = _ShortMessageStart -  56;
    static constexpr auto MessageEnd_Long  = _LongMessageStart  - 112;


    static constexpr auto OffsetMLAT_Short = std::max(MessageStart_Long - MessageStart_Short, 0) * 12;
    static constexpr auto OffsetMLAT_Long  = std::max(MessageStart_Short - MessageStart_Long, 0) * 12;

    static constexpr auto SameStart = (MessageStart_Long == MessageStart_Short);
};

typedef RegisterLayout<56, 112>  RegisterLayout_Right;
typedef RegisterLayout<128, 128> RegisterLayout_Left; 

template<int NumStreams>
class alignas(16) ShiftRegistersBase {
    public:

    ShiftRegistersBase() {
        for (auto i = 0; i < NumStreams; i++) {
            m_crc_56[i]  = 0;
            m_crc_112[i] = 0;
            m_high[i] = 0;
            m_low[i] = 0;
            m_df_56[i] = 0;
            m_df_112[i] = 0;
        };
    }

    constexpr const CRC::crc_t& getCRC_56(auto i) const  {
        return m_crc_56[i];
    }

    constexpr const CRC::crc_t& getCRC_112(auto i) const  {
        return m_crc_112[i];
    }

    constexpr const uint32_t& getDF_56(auto i) const {
        return m_df_56[i];
    }

    constexpr const uint32_t& getDF_112(auto i) const {
        return m_df_112[i];
    }

    constexpr const uint32_t& getDF(auto i) const {
        return m_df_112[i];
    }

    constexpr const uint64_t& high(auto i) const {
        return m_high[i];
    }

    constexpr const uint64_t& low(auto i) const {
        return m_low[i];
    }

    constexpr Bits128 extractFrame(auto i) const {
        return Bits128(m_high[i], m_low[i]);
    }

    protected:

    uint64_t m_low[NumStreams];    
    uint64_t m_high[NumStreams];
    
    uint32_t m_df_56[NumStreams];
    uint32_t m_df_112[NumStreams];

	// And a checksum for the short messages (56 bit)
	CRC::crc_t m_crc_56[NumStreams];

    // Each stream has a checksum for long messages (112 bit)
	CRC::crc_t m_crc_112[NumStreams];
};


template<int NumStreams, typename layout>
class alignas(16) ShiftRegisters : public ShiftRegistersBase<NumStreams> {
    public:
        ShiftRegisters() : ShiftRegistersBase<NumStreams>() { }

        constexpr void shiftInNewBits(uint32_t* cmp);
        constexpr Bits128 extractAlignedFrameLong(auto i) const;
        constexpr uint64_t extractAlignedFrameShort(auto i) const;
};

template<int NumStreams>
class alignas(16) ShiftRegisters<NumStreams, RegisterLayout_Right> : public ShiftRegistersBase<NumStreams> {
    public:

    constexpr void shiftInNewBits(uint32_t* cmp) {
        for (auto i = 0; i < NumStreams; i++) {
            // check if we shift out the 112th bit
			if (this->m_high[i] & (0x1ull << 47)) {
            	// adjust the 112 bit crc accordingly
				this->m_crc_112[i] ^= CRC::delta<111>();
			}

			// check if we shift out the 56th bit
			if (this->m_low[i] & (0x1ull << 55)) {
				// adjust the 56 bit crc
				this->m_crc_56[i] ^= CRC::delta<55>();
			}

			// now shift the complete frame one bit to the left
			// and add the new bit
            this->m_high[i] = (this->m_high[i] << 1) | (this->m_low[i] >> 63);
			this->m_low[i] = (this->m_low[i] << 1) | cmp[i];

			// do the same with the 56- and 112-bit crcs
			this->m_crc_112[i] = (this->m_crc_112[i] << 1) | cmp[i];
			this->m_crc_56[i]  = (this->m_crc_56[i]  << 1) | cmp[i];

            this->m_df_112[i]  = (this->m_high[i] >> 43) & 0b11111;
            this->m_df_56[i]   = (this->m_low[i]  >> 51) & 0b11111;
            
            if (this->m_crc_112[i] > 0xfffffful) {
				this->m_crc_112[i] ^= CRC::polynomial;
			}

			// same for the crc of the 56 bits
			if (this->m_crc_56[i] > 0xfffffful) {
				this->m_crc_56[i]  ^= CRC::polynomial;
			}   
        }
    }

    constexpr Bits128 extractAlignedFrameLong(auto i) const {
        return Bits128(this->m_high[i] & 0xffffffffffffull, this->m_low[i]);
    }

    constexpr uint64_t extractAlignedFrameShort(auto i) const {
        return this->m_low[i] & 0xffffffffffffffull;
    }
};

template<int NumStreams>
class alignas(16) ShiftRegisters<NumStreams, RegisterLayout_Left> : public ShiftRegistersBase<NumStreams> {
    public:
    
    constexpr void shiftInNewBits(uint32_t* cmp) {
        for (auto i = 0; i < NumStreams; i++) {
            //if (m_high[i] > 0x7fffffffffffffffull) {
            //if (m_high[i] & (0x1ull << 63)) {
            if (this->m_df_112[i] > 0xf) {
				// adjust the 56 bit crc
				this->m_crc_56[i]  ^= CRC::delta<55>();//  * (m_bits[i].high() >> 63);
                // adjust the 112 bit crc accordingly
				this->m_crc_112[i] ^= CRC::delta<111>();// * (m_bits[i].high() >> 63);
            };
          
            this->m_crc_56[i]  = (this->m_crc_56[i] << 1) | ((this->m_high[i] >> 7) & 0x1);
            this->m_crc_112[i] = (this->m_crc_112[i]<< 1) | ((this->m_low[i] >> 15) & 0x1);

            this->m_high[i] = (this->m_high[i] << 1) | (this->m_low[i] >> 63);
			this->m_low[i]  = (this->m_low[i] << 1) | cmp[i];           
            this->m_df_112[i] = this->m_high[i] >> 59;

            // same for the crc of the 56 bits
			if (this->m_crc_56[i] > 0xfffffful) {
				this->m_crc_56[i] ^= CRC::polynomial;
			}

            if (this->m_crc_112[i] > 0xfffffful) {
				this->m_crc_112[i] ^= CRC::polynomial;
			}
        }
    }

    constexpr Bits128 extractAlignedFrameLong(auto i) const {
        return Bits128(this->m_high[i] >> 16, (this->m_low[i] >> 16) | (this->m_high[i] << 48));
    }

    constexpr uint64_t extractAlignedFrameShort(auto i) const {
        return (this->m_high[i] >> 8);
    }
};

