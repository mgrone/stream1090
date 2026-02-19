/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

enum class InputFormatType {
    IQ_UINT8_RTL_SDR,
    IQ_UINT16_RAW_AIRSPY,
    IQ_FLOAT32
};

struct IQ_UINT8_RTL_SDR {
    using RawType = uint8_t;
    static constexpr InputFormatType id = InputFormatType::IQ_UINT8_RTL_SDR;

    static inline float convertScalar(uint8_t v) noexcept {
        constexpr float scale = 1.0f / 127.5f;
        return (float(v) - 127.5f) * scale;
    }
};

struct IQ_UINT16_RAW_AIRSPY {
    using RawType = uint16_t;
    static constexpr InputFormatType id = InputFormatType::IQ_UINT16_RAW_AIRSPY;

    static inline float convertScalar(uint16_t v) noexcept {
        constexpr float scale = (1.0f / 2047.5f);
        return (float(v) - 2047.5f) * scale;
    }
};

struct IQ_FLOAT32 {
    using RawType = float;
    static constexpr InputFormatType id = InputFormatType::IQ_FLOAT32;

    static inline float convertScalar(float v) noexcept {
        return v;
    }
};
