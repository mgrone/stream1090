/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include <iostream>

namespace hex_detail {
    // LUT construction for byte => 2 hex digits
    consteval std::array<char, 512> make_hex_table() {
        std::array<char, 512> t{};
        for (int i = 0; i < 256; ++i) {
            unsigned hi = (i >> 4) & 0xF;
            unsigned lo = i & 0xF;
            t[i*2]     = hi < 10 ? '0' + hi : 'A' + (hi - 10);
            t[i*2 + 1] = lo < 10 ? '0' + lo : 'A' + (lo - 10);
        }
        return t;
    }

    inline constexpr auto ByteToHex = make_hex_table();
}


class AVRWriter {
public:
    AVRWriter(std::ostream& out) : m_out(out) {
        std::ios::sync_with_stdio(false);
    }

    // Writes an AVR short frame with MLAT timestamp (no RSSI)
    void write_short_MLAT(uint64_t ts, uint64_t frameShort) {
        char* p = m_buf;

        *p++ = '@';
        p = write_hex_fixed<12>(p, ts & 0xffffffffffffull);
        p = write_hex_fixed<14>(p, frameShort & 0xffffffffffffffull);
        *p++ = ';';
        *p++ = '\n';

        m_out.write(m_buf, p - m_buf);
        m_out.flush();
    }

    // Writes an AVR long frame with MLAT timestamp (no RSSI)
    void write_long_MLAT(uint64_t ts, const Bits128& frame) {
        char* p = m_buf;

        *p++ = '@';
        p = write_hex_fixed<12>(p, ts & 0xffffffffffffull);
        p = write_hex_fixed<12>(p, frame.high() & 0xffffffffffffull);
        p = write_hex_fixed<16>(p, frame.low());
        *p++ = ';';
        *p++ = '\n';

        m_out.write(m_buf, p - m_buf);
        m_out.flush();
    }

    // Writes an AVR short frame with MLAT timestamp and RSSI
    void write_short_MLAT_RSSI(uint64_t ts, uint64_t frameShort, uint8_t rssi) {
        char* p = m_buf;

        *p++ = '<';
        p = write_hex_fixed<12>(p, ts & 0xffffffffffffull);
        p = write_hex_fixed<2>(p, rssi);
        p = write_hex_fixed<14>(p, frameShort & 0xffffffffffffffull);
        *p++ = ';';
        *p++ = '\n';

        m_out.write(m_buf, p - m_buf);
        m_out.flush();
    }

    // Writes an AVR long frame with MLAT timestamp and RSSI
    void write_long_MLAT_RSSI(uint64_t ts, const Bits128& frame, uint8_t rssi) {
        char* p = m_buf;

        *p++ = '<';
        p = write_hex_fixed<12>(p, ts & 0xffffffffffffull);
        p = write_hex_fixed<2>(p, rssi);
        p = write_hex_fixed<12>(p, frame.high() & 0xffffffffffffull);
        p = write_hex_fixed<16>(p, frame.low());
        *p++ = ';';
        *p++ = '\n';

        m_out.write(m_buf, p - m_buf);
        m_out.flush();
    }

private:
    template<int DIGITS>
    static inline char* write_hex_fixed(char* out, uint64_t value) {
        static_assert(DIGITS > 0);
        static_assert(DIGITS % 2 == 0);

        constexpr int BYTES = DIGITS / 2;
        int shift = (BYTES - 1) * 8;

        for (int i = 0; i < BYTES; ++i) {
            uint8_t b = (value >> shift) & 0xFF;
            const char* h = &hex_detail::ByteToHex[b * 2];
            *out++ = h[0];
            *out++ = h[1];
            shift -= 8;
        }

        return out;
    }

    // the buffer for the output
    char m_buf[96];

    // the stream to write ot
    std::ostream& m_out;
};



