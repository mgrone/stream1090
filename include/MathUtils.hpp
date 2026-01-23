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
#include "rtl_sdr_iq_sqrt_table.hpp"

namespace MathUtils {
	constexpr float sqrt_iq(uint16_t iq) {
		return iq_sqrt_table[iq];
	}

	constexpr float sqrt_iq(uint8_t i, uint8_t q) {
		return sqrt_iq((static_cast<uint16_t>(i) << 8) | q);
	}
} // namespace MathUtils
