/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "Bits128.hpp"
#include "CRC.hpp"

template<int NumStreams>
class alignas(16) ShiftRegistersBase {
    public:

    constexpr ShiftRegistersBase() noexcept {
        for (auto i = 0; i < NumStreams; i++) {
            m_crc_56[i]  = 0;
            m_crc_112[i] = 0;
            m_high[i] = 0;
            m_low[i] = 0;
            m_df[i] = 0;
        };
    }

    constexpr const CRC::crc_t& getCRC_56(auto i) const noexcept {
        return m_crc_56[i];
    }

    constexpr const CRC::crc_t& getCRC_112(auto i) const noexcept {
        return m_crc_112[i];
    }

    constexpr const uint32_t& getDF(auto i) const noexcept{
        return m_df[i];
    }

    protected:

    uint64_t m_low[NumStreams];    
    uint64_t m_high[NumStreams];
    
    uint32_t m_df[NumStreams];

	// And a checksum for the short messages (56 bit)
	CRC::crc_t m_crc_56[NumStreams];

    // Each stream has a checksum for long messages (112 bit)
	CRC::crc_t m_crc_112[NumStreams];
};


template<int NumStreams>
class alignas(16) ShiftRegisters : public ShiftRegistersBase<NumStreams> {
    public:
        constexpr ShiftRegisters() : ShiftRegistersBase<NumStreams>() { }

        constexpr void shiftInNewBits(const uint32_t* cmp) noexcept {
            for (auto i = 0; i < NumStreams; i++) {
                // check if we shift out the msb 
                if (this->m_df[i] > 0xf) {
                    // adjust the 56 bit crc
                    this->m_crc_56[i]  ^= CRC::delta<55>();//  * (m_bits[i].high() >> 63);
                    // adjust the 112 bit crc accordingly
                    this->m_crc_112[i] ^= CRC::delta<111>();// * (m_bits[i].high() >> 63);
                };
            
                this->m_crc_56[i]  = (this->m_crc_56[i] << 1) | ((this->m_high[i] >> 7) & 0x1);
                this->m_crc_112[i] = (this->m_crc_112[i]<< 1) | ((this->m_low[i] >> 15) & 0x1);

                this->m_high[i] = (this->m_high[i] << 1) | (this->m_low[i] >> 63);
                this->m_low[i]  = (this->m_low[i] << 1) | cmp[i];           
                this->m_df[i] = this->m_high[i] >> 59;

                // if the current crc is too large, shorten it with the polynomial
                if (this->m_crc_56[i] > 0xfffffful) {
                    this->m_crc_56[i] ^= CRC::polynomial;
                }

                 // same for the crc of the 112 bits
                if (this->m_crc_112[i] > 0xfffffful) {
                    this->m_crc_112[i] ^= CRC::polynomial;
                }
            }
        }

        constexpr Bits128 extractAlignedFrameLong(auto i) const noexcept {
            return Bits128(this->m_high[i] >> 16, (this->m_low[i] >> 16) | (this->m_high[i] << 48));
        }

        constexpr uint64_t extractAlignedFrameShort(auto i) const noexcept {
            return (this->m_high[i] >> 8);
        }
};


