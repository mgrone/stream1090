/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include "Bits128.hpp"
#include "CRC.hpp"

// Downlink formats the demodulator actually looks at: 0, 4, 5, 11, 16, 17, 18,
// 19, 20, 21. Every other value is dropped right away, so the shift register
// can tell the caller which streams are worth a closer look and which are not.
inline constexpr uint32_t handledDownlinkFormats =
      (1u <<  0) | (1u <<  4) | (1u <<  5) | (1u << 11)
    | (1u << 16) | (1u << 17) | (1u << 18) | (1u << 19)
    | (1u << 20) | (1u << 21);

template<int NumStreams>
class alignas(16) ShiftRegistersBase {
    public:

    // one bit per stream has to fit into the returned mask
    static_assert(NumStreams <= 64, "stream mask is 64 bit wide");

    constexpr ShiftRegistersBase() noexcept {
        for (auto i = 0; i < NumStreams; i++) {
            m_crc_56[i]  = 0;
            m_crc_112[i] = 0;
            m_high[i] = 0;
            m_low[i] = 0;
        };
    }

    constexpr CRC::crc_t getCRC_56(auto i) const noexcept {
        return (CRC::crc_t)m_crc_56[i];
    }

    constexpr CRC::crc_t getCRC_112(auto i) const noexcept {
        return (CRC::crc_t)m_crc_112[i];
    }

    /// The downlink format is the top five bits of the register, so keeping a
    /// separate array for it meant storing NumStreams words every sample to
    /// serve a read that only happens on a handled format. Derived here
    /// instead, from the value the update has already written.
    constexpr uint32_t getDF(auto i) const noexcept{
        return (uint32_t)(m_high[i] >> 59);
    }

    protected:

    uint64_t m_low[NumStreams];
    uint64_t m_high[NumStreams];

	// And a checksum for the short messages (56 bit). Held in 64 bit so the
	// masked update below needs no narrowing in the middle of the chain.
	uint64_t m_crc_56[NumStreams];

    // Each stream has a checksum for long messages (112 bit)
	uint64_t m_crc_112[NumStreams];
};


template<int NumStreams>
class alignas(16) ShiftRegisters : public ShiftRegistersBase<NumStreams> {
    public:
        constexpr ShiftRegisters() : ShiftRegistersBase<NumStreams>() { }

        /// Shifts one new bit into every stream and updates both CRCs.
        /// Returns a mask with bit i set when stream i now shows a downlink
        /// format the demodulator handles.
        ///
        /// The two conditionals of the textbook version (did we shift a one out
        /// of the register, did the CRC overflow past 24 bits) are replaced by
        /// masks. Both are close to coin flips, so as branches they were mostly
        /// mispredictions, and without them the loop has no control flow left
        /// for the compiler to trip over.
        uint64_t shiftInNewBits(const uint32_t* cmp) noexcept {
            uint64_t handledMask = 0;

            for (auto i = 0; i < NumStreams; i++) {
                const uint64_t high = this->m_high[i];
                const uint64_t low  = this->m_low[i];

                // all ones when a one bit leaves the register at the top
                const uint64_t shiftedOut = uint64_t(0) - (high >> 63);

                uint64_t crc56  = this->m_crc_56[i]  ^ (Delta55  & shiftedOut);
                uint64_t crc112 = this->m_crc_112[i] ^ (Delta111 & shiftedOut);

                crc56  = (crc56  << 1) | ((high >> 7)  & 0x1);
                crc112 = (crc112 << 1) | ((low  >> 15) & 0x1);

                const uint64_t newHigh = (high << 1) | (low >> 63);
                const uint64_t newLow  = (low  << 1) | cmp[i];
                const uint64_t df      = newHigh >> 59;

                // Fold the polynomial back in whenever bit 24 is set. The CRC
                // never exceeds 25 bits here, so the shift is a 0/1 flag.
                crc56  ^= Polynomial & (uint64_t(0) - (crc56  >> 24));
                crc112 ^= Polynomial & (uint64_t(0) - (crc112 >> 24));

                this->m_crc_56[i]  = crc56;
                this->m_crc_112[i] = crc112;
                this->m_high[i]    = newHigh;
                this->m_low[i]     = newLow;

                handledMask |= uint64_t((handledDownlinkFormats >> df) & 0x1) << i;
            }

            return handledMask;
        }

        constexpr Bits128 extractAlignedFrameLong(auto i) const noexcept {
            return Bits128(this->m_high[i] >> 16, (this->m_low[i] >> 16) | (this->m_high[i] << 48));
        }

        constexpr uint64_t extractAlignedFrameShort(auto i) const noexcept {
            return (this->m_high[i] >> 8);
        }

    private:
        static constexpr uint64_t Delta55  = CRC::delta<55>();
        static constexpr uint64_t Delta111 = CRC::delta<111>();
        static constexpr uint64_t Polynomial = CRC::polynomial;

};
