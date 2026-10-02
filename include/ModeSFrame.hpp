/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "Bits128.hpp"

#include <cstdint>
#include <type_traits>

enum class ModeSFrameLength : uint8_t { Short = 56, Long = 112 };

struct ModeSFrame {
    uint64_t mlatTimestamp = 0;
    uint64_t high = 0;
    uint64_t low = 0;
    uint8_t signalLevel = 0;
    ModeSFrameLength length = ModeSFrameLength::Short;
    bool signalAvailable = false;

    static constexpr ModeSFrame shortFrame(uint64_t timestamp, uint64_t payload, uint8_t signal,
                                            bool hasSignal) noexcept {
        return {timestamp & 0xFFFFFFFFFFFFULL, 0, payload & 0x00FFFFFFFFFFFFFFULL, signal,
                ModeSFrameLength::Short, hasSignal};
    }

    static constexpr ModeSFrame longFrame(uint64_t timestamp, const Bits128& payload, uint8_t signal,
                                           bool hasSignal) noexcept {
        return {timestamp & 0xFFFFFFFFFFFFULL, payload.high() & 0xFFFFFFFFFFFFULL, payload.low(), signal,
                ModeSFrameLength::Long, hasSignal};
    }

    constexpr bool operator==(const ModeSFrame&) const noexcept = default;
};

// The SPSC queue passes these by value between two threads, so a frame must be
// a plain value with no hidden indirection.
static_assert(std::is_trivially_copyable_v<ModeSFrame>,
              "ModeSFrame must stay trivially copyable for the SPSC handoff");
