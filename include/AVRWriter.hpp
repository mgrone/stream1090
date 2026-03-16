/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <iostream>
#include <format>

class AVRWriter {
public:
    AVRWriter(std::ostream& out) : m_out(out) {
        // Disable C I/O sync globally. Helps reduce overhead when flushing frequently.
        std::ios::sync_with_stdio(false);
    }

    // Writes an AVR short frame with MLAT timestamp (no RSSI)
    void write_short_MLAT(uint64_t ts, uint64_t frameShort) {
        // Format (short frame, 56-bit):
        //   @TTTTTTTTTTTTFFFFFFFFFFFFFF;
        //
        // Where:
        //   '@'                       = MLAT marker (no RSSI)
        //   TTTTTTTTTTTT (12 hex)     = 48-bit MLAT timestamp
        //   FFFFFFFFFFFFFF (14 hex)   = 56-bit Mode-S short frame
        //   ';' + '\n'                = terminator
        //
        auto result = std::format_to_n(
            m_buf, sizeof(m_buf),
            "@{:012X}{:014X};\n",
            ts & 0xffffffffffffull,
            frameShort & 0xffffffffffffffull
        );
        m_out.write(m_buf, result.out - m_buf);
        m_out.flush();
    }

    // Writes an AVR long frame with MLAT timestamp (no RSSI)
    void write_long_MLAT(uint64_t ts, const Bits128& frame) {
        // Format (long frame, 112-bit):
        //   @TTTTTTTTTTTTHHHHHHHHHHHHLLLLLLLLLLLLLLLL;
        //
        // Where:
        //   '@'                       = MLAT marker (no RSSI)
        //   TTTTTTTTTTTT (12 hex)     = 48-bit MLAT timestamp
        //   HHHHHHHHHHHH (12 hex)     = upper 48 bits of 112-bit frame
        //   LLLLLLLLLLLLLLLL (16 hex) = lower 64 bits of 112-bit frame
        //   ';' + '\n'                = terminator
        //
        auto result = std::format_to_n(
            m_buf, sizeof(m_buf),
            "@{:012X}{:012X}{:016X};\n",
            ts & 0xffffffffffffull,
            frame.high() & 0xffffffffffffull,
            frame.low()
        );
        m_out.write(m_buf, result.out - m_buf);
        m_out.flush();
    }

    // Writes an AVR short frame with MLAT timestamp and RSSI
    void write_short_MLAT_RSSI(uint64_t ts, uint64_t frameShort, uint8_t rssi) {
        // Format (short frame, 56-bit + RSSI):
        //   <TTTTTTTTTTTTRRFFFFFFFFFFFFFF;
        //
        // Where:
        //   '<'                       = MLAT marker WITH RSSI
        //   TTTTTTTTTTTT (12 hex)     = 48-bit MLAT timestamp
        //   RR (2 hex)                = 8-bit RSSI value
        //   FFFFFFFFFFFFFF (14 hex)   = 56-bit Mode-S short frame
        //   ';' + '\n'                = terminator
        //
        auto result = std::format_to_n(
            m_buf, sizeof(m_buf),
            "<{:012X}{:02X}{:014X};\n",
            ts & 0xffffffffffffull,
            static_cast<unsigned>(rssi),
            frameShort & 0xffffffffffffffull
        );
        m_out.write(m_buf, result.out - m_buf);
        m_out.flush();
    }

    // Writes an AVR long frame with MLAT timestamp and RSSI
    void write_long_MLAT_RSSI(uint64_t ts, const Bits128& frame, uint8_t rssi) {
        // Format (long frame, 112-bit + RSSI):
        //   <TTTTTTTTTTTTRRHHHHHHHHHHHHLLLLLLLLLLLLLLLL;
        //
        // Where:
        //   '<'                       = MLAT marker WITH RSSI
        //   TTTTTTTTTTTT (12 hex)     = 48-bit MLAT timestamp
        //   RR (2 hex)                = 8-bit RSSI value
        //   HHHHHHHHHHHH (12 hex)     = upper 48 bits of 112-bit frame
        //   LLLLLLLLLLLLLLLL (16 hex) = lower 64 bits of 112-bit frame
        //   ';' + '\n'                = terminator
        //
        auto result = std::format_to_n(
            m_buf, sizeof(m_buf),
            "<{:012X}{:02X}{:012X}{:016X};\n",
            ts & 0xffffffffffffull,
            static_cast<unsigned>(rssi),
            frame.high() & 0xffffffffffffull,
            frame.low()
        );
        m_out.write(m_buf, result.out - m_buf);
        m_out.flush();
    }

private:
    char m_buf[96];
    std::ostream& m_out;
};


