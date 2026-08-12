/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <cstdint>

/*
 * The DSP chain runs in fixed point. Samples are int16 in Q14, so 16384 is
 * full scale and there is a bit of headroom left in the type for a stage that
 * overshoots. The conversions below are exact integer arithmetic; the half LSB
 * of centring that the float versions carried is dropped, and the DC removal
 * takes it out anyway.
 */
inline constexpr int SampleFracBits = 14;
inline constexpr int SampleOne      = 1 << SampleFracBits;

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

    // 8 bit unsigned centred on 127.5, scaled to Q14. The centre is half an
    // LSB below an integer, so the sample is doubled first and 255 subtracted:
    // that keeps the arithmetic exact and matches convertScalar above. Centring
    // on 128 instead costs about 2.7% of messages on a real dongle, because
    // half an LSB is a large fraction of an eight bit sample.
    static inline int16_t convertFixed(uint8_t v) noexcept {
        return int16_t((int32_t(v) * 2 - 255) * (SampleOne / 256));
    }
};

struct IQ_UINT16_RAW_AIRSPY {
    using RawType = uint16_t;
    static constexpr InputFormatType id = InputFormatType::IQ_UINT16_RAW_AIRSPY;

    static inline float convertScalar(uint16_t v) noexcept {
        constexpr float scale = (1.0f / 2047.5f);
        return (float(v) - 2047.5f) * scale;
    }

    // 12 bit unsigned centred on 2047.5, scaled to Q14, same trick as above.
    // Here it makes no measurable difference, because this format's -q pipeline
    // runs DCRemoval and that absorbs the half LSB either way; it is done for
    // consistency with convertScalar rather than for yield.
    static inline int16_t convertFixed(uint16_t v) noexcept {
        return int16_t((int32_t(v) * 2 - 4095) * (SampleOne / 4096));
    }
};

struct IQ_FLOAT32 {
    using RawType = float;
    static constexpr InputFormatType id = InputFormatType::IQ_FLOAT32;

    static inline float convertScalar(float v) noexcept {
        return v;
    }

    static inline int16_t convertFixed(float v) noexcept {
        const float x = v * float(SampleOne);
        return int16_t(x < -32768.0f ? -32768.0f : (x > 32767.0f ? 32767.0f : x));
    }
};
