/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include <iostream>

class AVRWriter {
public:
    AVRWriter(std::ostream& out) : m_out(out) {
        std::ios::sync_with_stdio(false);
    }

    // Writes an AVR short frame with MLAT timestamp (no RSSI)
    void write_short_MLAT(uint64_t ts, uint64_t frameShort) {
        char* p = m_buf;

        *p++ = '@';
        p = write_hex_fixed(p, ts & 0xffffffffffffull, 12);
        p = write_hex_fixed(p, frameShort & 0xffffffffffffffull, 14);
        *p++ = ';';
        *p++ = '\n';

        m_out.write(m_buf, p - m_buf);
        m_out.flush();
    }

    // Writes an AVR long frame with MLAT timestamp (no RSSI)
    void write_long_MLAT(uint64_t ts, const Bits128& frame) {
        char* p = m_buf;

        *p++ = '@';
        p = write_hex_fixed(p, ts & 0xffffffffffffull, 12);
        p = write_hex_fixed(p, frame.high() & 0xffffffffffffull, 12);
        p = write_hex_fixed(p, frame.low(), 16);
        *p++ = ';';
        *p++ = '\n';

        m_out.write(m_buf, p - m_buf);
        m_out.flush();
    }

    // Writes an AVR short frame with MLAT timestamp and RSSI
    void write_short_MLAT_RSSI(uint64_t ts, uint64_t frameShort, uint8_t rssi) {
        char* p = m_buf;

        *p++ = '<';
        p = write_hex_fixed(p, ts & 0xffffffffffffull, 12);
        p = write_hex_fixed(p, rssi, 2);
        p = write_hex_fixed(p, frameShort & 0xffffffffffffffull, 14);
        *p++ = ';';
        *p++ = '\n';

        m_out.write(m_buf, p - m_buf);
        m_out.flush();
    }

    // Writes an AVR long frame with MLAT timestamp and RSSI
    void write_long_MLAT_RSSI(uint64_t ts, const Bits128& frame, uint8_t rssi) {
        char* p = m_buf;

        *p++ = '<';
        p = write_hex_fixed(p, ts & 0xffffffffffffull, 12);
        p = write_hex_fixed(p, rssi, 2);
        p = write_hex_fixed(p, frame.high() & 0xffffffffffffull, 12);
        p = write_hex_fixed(p, frame.low(), 16);
        *p++ = ';';
        *p++ = '\n';

        m_out.write(m_buf, p - m_buf);
        m_out.flush();
    }

private:
    static char hex_digit(unsigned v) {
        return v < 10 ? char('0' + v) : char('A' + (v - 10));
    }

    static char* write_hex_fixed(char* out, uint64_t value, int digits) {
        for (int i = digits - 1; i >= 0; --i) {
            out[i] = hex_digit(value & 0xF);
            value >>= 4;
        }
        return out + digits;
    }

    char m_buf[96];
    std::ostream& m_out;
};



