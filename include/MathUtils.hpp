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

    constexpr double constSqrtNR(double x) {
        double curr = x;
        for (int i = 0; i < 10; i++) {
            curr = 0.5 * (curr + x / curr);
        }
        return curr;
    }

    constexpr auto fillSqrtTable() {
        std::array<float, 65536> table{};

        for (int i = 0; i < 256; i++) {
            for (int q = 0; q < 256; q++) {

                const double f_i = (static_cast<double>(i) - 127.5) / 127.5;
                const double f_q = (static_cast<double>(q) - 127.5) / 127.5;
                const double sq = f_i * f_i + f_q * f_q;

                table[(i << 8) | q] = static_cast<float>(constSqrtNR(sq));
            }
        }
        return table;
    }

    inline constexpr auto iq_sqrt_table = fillSqrtTable();
}

constexpr float sqrt_iq(uint16_t iq) {
    return Details::iq_sqrt_table[iq];
}

constexpr float sqrt_iq(uint8_t i, uint8_t q) {
    return sqrt_iq((i << 8) | q);
}

} // namespace MathUtils
