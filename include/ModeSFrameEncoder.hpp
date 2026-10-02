/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "ModeSFrame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// Worst case, computed rather than assumed so the buffer cannot silently
// become too small if a field is ever added:
//   AVR  '<' + 12 timestamp + 2 signal + 28 payload + ';' + '\n'
inline constexpr size_t kMaxAvrEncodedBytes = 1 + 12 + 2 + 28 + 2;
inline constexpr size_t kEncodedFrameCapacity = 64;
static_assert(kMaxAvrEncodedBytes <= kEncodedFrameCapacity, "AVR frame does not fit the encode buffer");

struct EncodedModeSFrame {
    std::array<uint8_t, kEncodedFrameCapacity> bytes{};
    size_t size = 0;
};

namespace ModeSFrameEncoderDetail {
inline constexpr char Hex[] = "0123456789ABCDEF";

inline void appendHex(EncodedModeSFrame& out, uint64_t value, unsigned digits) noexcept {
    for (unsigned shift = digits * 4; shift != 0; shift -= 4)
        out.bytes[out.size++] = uint8_t(Hex[(value >> (shift - 4)) & 0xF]);
}

} // namespace ModeSFrameEncoderDetail

inline EncodedModeSFrame encodeAvr(const ModeSFrame& frame) noexcept {
    using namespace ModeSFrameEncoderDetail;
    EncodedModeSFrame out;
    out.bytes[out.size++] = frame.signalAvailable ? '<' : '@';
    appendHex(out, frame.mlatTimestamp & 0xFFFFFFFFFFFFULL, 12);
    if (frame.signalAvailable)
        appendHex(out, frame.signalLevel, 2);
    if (frame.length == ModeSFrameLength::Short) {
        appendHex(out, frame.low & 0x00FFFFFFFFFFFFFFULL, 14);
    } else {
        appendHex(out, frame.high & 0xFFFFFFFFFFFFULL, 12);
        appendHex(out, frame.low, 16);
    }
    out.bytes[out.size++] = ';';
    out.bytes[out.size++] = '\n';
    return out;
}
