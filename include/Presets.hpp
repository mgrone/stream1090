/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include "Sampler.hpp"
#include "IQPipeline.hpp"
#include "LowPassFilter.hpp"


enum class IQPipelineOptions {
    NONE,
    IQ_FIR,
    IQ_FIR_FILE
};

template<typename Raw, SampleRate In, SampleRate Out, IQPipelineOptions Opt>
struct Preset {
    using RawType = Raw;
    static constexpr SampleRate        inputRate      = In;
    static constexpr SampleRate        outputRate     = Out;
    static constexpr IQPipelineOptions pipelineOption = Opt;
};

constexpr auto presets = std::make_tuple(
    Preset<uint8_t,  Rate_2_4_Mhz, Rate_8_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<uint16_t, Rate_6_0_Mhz, Rate_6_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<uint16_t, Rate_6_0_Mhz, Rate_12_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<uint16_t, Rate_6_0_Mhz, Rate_24_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<uint16_t, Rate_10_0_Mhz, Rate_10_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<uint16_t, Rate_10_0_Mhz, Rate_24_0_Mhz, IQPipelineOptions::NONE>{},

    Preset<uint8_t,  Rate_2_4_Mhz, Rate_8_0_Mhz, IQPipelineOptions::IQ_FIR>{},
    Preset<uint16_t, Rate_6_0_Mhz, Rate_6_0_Mhz, IQPipelineOptions::IQ_FIR>{},
    Preset<uint16_t, Rate_6_0_Mhz, Rate_12_0_Mhz, IQPipelineOptions::IQ_FIR>{},
    Preset<uint16_t, Rate_6_0_Mhz, Rate_24_0_Mhz, IQPipelineOptions::IQ_FIR>{},
    Preset<uint16_t, Rate_10_0_Mhz, Rate_10_0_Mhz, IQPipelineOptions::IQ_FIR>{},
    Preset<uint16_t, Rate_10_0_Mhz, Rate_24_0_Mhz, IQPipelineOptions::IQ_FIR>{},

    Preset<uint8_t,  Rate_2_4_Mhz, Rate_8_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{},
    Preset<uint16_t, Rate_6_0_Mhz, Rate_6_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{},
    Preset<uint16_t, Rate_6_0_Mhz, Rate_12_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{},
    Preset<uint16_t, Rate_6_0_Mhz, Rate_24_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{},
    Preset<uint16_t, Rate_10_0_Mhz, Rate_10_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{},
    Preset<uint16_t, Rate_10_0_Mhz, Rate_24_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{}
);

enum class RawFormat {
    UINT_8_IQ,
    UINT_16_IQ,
    FLOAT_32_IQ
};

/*template<RawFormat rawFormat>
struct RawFormatToType { };

template<> RawFormatToType<RawFormat::UINT_8_IQ> { using type = uint8_t; };
template<> RawFormatToType<RawFormat::UINT_16_IQ> { using type = uint16_t; };
template<> RawFormatToType<RawFormat::FLOAT_32_IQ> { using type = float; };
*/


template<SampleRate In, SampleRate Out, IQPipelineOptions sel>
struct IQPipelineSelector {
    static auto make(const std::vector<float>&) {
        return make_pipeline();
    }
};

template<SampleRate In, SampleRate Out>
struct IQPipelineSelector<In, Out, IQPipelineOptions::IQ_FIR> {
    static auto make(const std::vector<float>&) {
        return make_pipeline(DCRemoval(), FlipSigns(), IQLowPass<In, Out>());
    }
};

template<SampleRate In, SampleRate Out>
struct IQPipelineSelector<In, Out, IQPipelineOptions::IQ_FIR_FILE> {
    static auto make(const std::vector<float>& taps) {
        return make_pipeline(DCRemoval(), FlipSigns(), IQLowPassDynamic(taps));
    }
};

