/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once 

#include <array>
#include <cstdint>
#include <cmath>

namespace MathUtils {
	namespace Details {
		// constexpr approximation of the square root
		constexpr double constSqrtNR(double x, double curr, double prev) {
			return (curr == prev) ? curr : constSqrtNR(x, 0.5 * (curr + x / curr), curr);
		}

		// compute all possible values for an 8-bit iq pair
		constexpr auto fillSqrtTable() {
			std::array<float, 65536> table{ 0 };
			for (auto i = 0; i < 256; i++) {
				for (auto q = 0; q < 256; q++) {
					// make the iq bytes from the input signed and between -1.0 ... 1.0
					const double f_i = ((double)i - 127.5) / 127.5;
					const double f_q = ((double)q - 127.5) / 127.5;
					const double sq = f_i * f_i + f_q * f_q;
					// we dont care about endianness here due to symmetry
					table[(static_cast<std::array<float, 65536>::size_type>(i) << 8) | q] = (float)constSqrtNR(sq, sq, 0.0);
				}
			}
			return table;
		}

		// the LUT used for the square root
		inline constexpr auto iq_sqrt_table{ fillSqrtTable() };
	}

	constexpr float sqrt_iq(uint16_t iq) {
		return Details::iq_sqrt_table[iq];
	}

	constexpr float sqrt_iq(uint8_t i, uint8_t q) {
		return sqrt_iq((i << 8) | q);
	}
}
