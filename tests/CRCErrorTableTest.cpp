/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "CRCErrorTable.hpp"

namespace {

constexpr bool finds(const auto& table, uint8_t pattern, uint8_t index) {
    const auto expected = CRC::encodeFixOp(pattern, index);
    const auto actual = table.lookup(CRC::compute(expected));
    return actual.pattern == expected.pattern && actual.index == expected.index;
}

constexpr bool validatesDF17Table() {
    for (int i = 0; i < 112 - 5; ++i)
        if (!finds(CRC::df17ErrorTable, 0x1, i)) return false;
    for (int i = 0; i < 111 - 5; ++i)
        if (!finds(CRC::df17ErrorTable, 0x3, i)) return false;
    for (int i = 0; i < 110 - 5; ++i)
        if (!finds(CRC::df17ErrorTable, 0x7, i)) return false;
    for (int i = 0; i < 105 - 5; ++i)
        if (!finds(CRC::df17ErrorTable, 129, i)) return false;
    for (int i = 0; i < 110 - 5; ++i)
        if (!finds(CRC::df17ErrorTable, 0x5, i)) return false;
    for (int i = 0; i < 109 - 5; ++i)
        if (!finds(CRC::df17ErrorTable, 0x9, i)) return false;
    for (int i = 0; i < 108 - 5; ++i)
        if (!finds(CRC::df17ErrorTable, 0x11, i)) return false;
    for (int i = 0; i < 107 - 5; ++i)
        if (!finds(CRC::df17ErrorTable, 0x21, i)) return false;
    for (int i = 0; i < 106 - 5; ++i)
        if (!finds(CRC::df17ErrorTable, 0x41, i)) return false;
    return true;
}

constexpr bool validatesDF11Table() {
    for (int i = 0; i < 56 - 5; ++i)
        if (!finds(CRC::df11ErrorTable, 0x1, i)) return false;
    for (int i = 0; i < 55 - 5; ++i)
        if (!finds(CRC::df11ErrorTable, 0x3, i)) return false;
    return true;
}

static_assert(validatesDF17Table());
static_assert(validatesDF11Table());

} // namespace

int main() {}
