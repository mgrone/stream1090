/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdint>
#include "Bits128.hpp"

namespace BitUtils {

    template<std::size_t hi, std::size_t lo>
    struct BitRange {
        static_assert(lo <= hi);
        static constexpr std::size_t HI = hi;
        static constexpr std::size_t LO = lo;
    };

    template<typename ResType, std::size_t hi, std::size_t lo>
    constexpr ResType getBits(uint64_t bits) noexcept {
        static_assert(lo <= hi);
        static_assert(hi < 64);

        constexpr uint64_t width = hi - lo + 1;
        static_assert(sizeof(ResType) * 8 >= width);

        constexpr uint64_t mask = (width == 64) ? ~uint64_t{0} : ((uint64_t{1} << width) - 1);

        return ResType((bits >> lo) & mask);
    }

    template<typename ResType, std::size_t hi, std::size_t lo>
    constexpr ResType getBits(const uint64_t& high, const uint64_t& low) noexcept {
        static_assert(lo <= hi);
        static_assert(hi < 128);
        static_assert((hi - lo) < 64);

        constexpr uint64_t width = hi - lo + 1;
        static_assert(sizeof(ResType) * 8 >= width);

// C++20 conditional block: Checks if the target platform natively supports 128-bit integers
#if defined(__SIZEOF_INT128__)
        // take the easy road
        __uint128_t x = ( (__uint128_t)high << 64 ) | low;
        constexpr uint64_t mask = (width == 64) ? ~uint64_t{0} : ((uint64_t{1} << width) - 1);
        return ResType((x >> lo) & mask);
#else
        // well then the long road
        constexpr uint64_t mask = (width == 64) ? ~uint64_t{0} : ((uint64_t{1} << width) - 1);
        uint64_t result = 0;

        if constexpr (lo >= 64) {
            result = high >> (lo - 64);
        } 
        else if constexpr (hi < 64) {
            result = low >> lo;
        } 
        else {
            constexpr std::size_t low_bits_count = 64 - lo;
            result = (low >> lo) | (high << low_bits_count);
        }
        return ResType(result & mask);
    #endif
    }


    template<typename ResType, std::size_t hi, std::size_t lo>
    constexpr ResType getBits(const Bits128& bits) noexcept {
        return getBits<ResType, hi, lo>(bits.high(), bits.low());
    }

    template<typename ResType, typename Range>
    constexpr ResType getBits(uint64_t bits) noexcept {
        return getBits<ResType, Range::HI, Range::LO>(bits);
    }

    template<typename ResType, typename Range>
    constexpr ResType getBits(uint64_t high, uint64_t low) noexcept {
        return getBits<ResType, Range::HI, Range::LO>(high, low);
    }

    template<typename ResType, typename Range>
    constexpr ResType getBits(const Bits128& bits) noexcept {
        return getBits<ResType, Range::HI, Range::LO>(bits);
    }
} // end of namespace
